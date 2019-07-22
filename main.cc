#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>

#include <utility>
#include <sstream>
#include <string>
#include <memory>
#include <functional>
#include <vector>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <zlib.h>

namespace udon
{

class Fd
{
 public:
  Fd(const char* path, int oflag)
  {
    fd_ = open(path, oflag);
  }

  virtual ~Fd()
  {
    close(fd_);
  }

  /* 0: succ
   * 1: open failed
   * 2: read failed
   * */
  int Load(std::string* data)
  {
    static const int READ_SIZE = BUFSIZ;
    int ret = 0;
    if (fd_ < 0)
    {
      return 1;
    }
    char buf[READ_SIZE] = "";
    for (;;)
    {
      ret = read(fd_, buf, READ_SIZE);
      if (ret > 0)
      {
        data->append(buf, ret);
      }
      else if (ret == 0)
      {
        break;
      }
      else
      {
        return 2;
      }
    }
    return 0;
  }

 protected:
  int fd_;
};

namespace scr
{
namespace benchmark
{

struct Chunk
{
  union Meta
  {
    struct __attribute__((packed)) Data
    {
      unsigned int check;
      unsigned int count;
    } data;
    char buf[sizeof(data)];
  } meta;
  union Len
  {
    int data;
    char buf[sizeof(data)];
  };
  std::vector<std::string> datas;
  std::string raw;
};

int ParseChunk(const char* data, int size, Chunk* chunk)
{
  int ret = 0;
  int read_len = 0;
  if (size < sizeof(chunk->meta))
  {
    return -1;
  }
  memcpy(chunk->meta.buf, data+read_len, sizeof(chunk->meta));
  read_len += sizeof(chunk->meta);

  // fprintf(stderr, "check=%u count=%u\n", chunk->meta.data.check, chunk->meta.data.count);

  chunk->datas.resize(chunk->meta.data.count);
  for (int i = 0; i < chunk->meta.data.count; i++)
  {
    Chunk::Len len = {};
    if (size < read_len+sizeof(len))
    {
      fprintf(stderr, "read len failed\n");
      return -2;
    }
    memcpy(len.buf, data+read_len, sizeof(len));
    read_len += sizeof(len);
    if (size < read_len+len.data)
    {
      fprintf(stderr, "read data failed: len=%d\n", len.data);
      return -3;
    }
    chunk->datas[i].assign(data+read_len, len.data);
    read_len += len.data;

    // fprintf(stderr, "0x%hhx%hhx\n", chunk->datas[i][0], chunk->datas[i][1]);
    if (chunk->datas[i].substr(0, 2) == "\x78\x9c")
    {
      z_stream zstream = {};
      ret = inflateInit(&zstream);
      if (ret != Z_OK)
      {
        return -4;
      }

      char buf[BUFSIZ] = "";
      zstream.avail_in = chunk->datas[i].size();
      zstream.next_in = reinterpret_cast<unsigned char*>(const_cast<char*>(chunk->datas[i].data()));
      for (;;)
      {
        zstream.avail_out = BUFSIZ;
        zstream.next_out = reinterpret_cast<unsigned char*>(buf);
        ret = inflate(&zstream, Z_NO_FLUSH);
        if (ret == Z_OK)
        {
        }
        else if (ret == Z_STREAM_END)
        {
        }
        else
        {
          fprintf(stderr, "%d: %s\n", ret, zError(ret));
          inflateEnd(&zstream);
          return -5;
        }
        
        chunk->raw.append(buf, BUFSIZ-zstream.avail_out);
        // fprintf(stderr, "have=%d avalid_out=%d\n", BUFSIZ-zstream.avail_out, zstream.avail_out);
        if (ret == Z_STREAM_END)
        {
          break;
        }
      }
      inflateEnd(&zstream);
    }
    else
    {
      chunk->raw.append(chunk->datas[i]);
    }
  }

  return read_len;
};

void DumpChunk(const char* loghd, const Chunk& chunk)
{
  printf("%scheck=%u\n", loghd, chunk.meta.data.check);
  printf("%scount=%u\n", loghd, chunk.meta.data.count);
  for (int i = 0; i < chunk.datas.size(); i++)
  {
    printf("%s\tdata[%d].size()=%ld\n", loghd, i, chunk.datas[i].size());
    // printf("%s\tdata[%d] = %s\n", loghd, i, chunk.datas[i].substr(0, 10).c_str());
  }
}

struct Replay
{
  std::string replayid;
  unsigned int u;
  union Header
  {
    struct __attribute__((packed)) Data
    {
      unsigned char engine;
      unsigned int game_frames;
      unsigned char u1[3];
      unsigned int save_time;
      unsigned char u2[12];
      unsigned char game_name[28];
      unsigned short map_width;
      unsigned short map_height;
      unsigned char u3[16];
      unsigned char creator[24];
      unsigned char u4;
      unsigned char map_name[26];
      unsigned char u5[38];
      unsigned char player_records[432];
      unsigned int player_color[8];
      unsigned char player_index[8];
    } data;
    char buf[sizeof(data)];
  }
  header;
};

struct PlayerCommand
{
  union Time
  {
    struct __attribute__((packed)) Data
    {
      unsigned int pasted;
      unsigned char command_len;
    } data;
    char buf[sizeof(data)];
  } time;
  struct Command
  {
    union Head
    {
      struct __attribute__((packed)) Data
      {
        unsigned char playerid;
        unsigned char cmdid;
      } data;
      char buf[sizeof(data)];
    } header;
    union Detail
    {
      union Select
      {
        struct __attribute__((packed)) Data
        {
          unsigned char unit_cnt;
          unsigned short unit[1<<(sizeof(unit_cnt)<<3)];
        } data;
        char buf[sizeof(data)];
      } select;
      union ShiftSelect
      {
        struct __attribute__((packed)) Data
        {
          unsigned char unit_cnt;
          unsigned short unit[1<<(sizeof(unit_cnt)<<3)];
        } data;
        char buf[sizeof(data)];
      } shift_select;
      union ShiftDelect
      {
        struct __attribute__((packed)) Data
        {
          unsigned char unit_cnt;
          unsigned short unit[1<<(sizeof(unit_cnt)<<3)];
        } data;
        char buf[sizeof(data)];
      } shift_delect;
      union Build
      {
        struct __attribute__((packed)) Data
        {
          unsigned char build_unit_type;
          unsigned short x;
          unsigned short y;
          unsigned short build_unitid;
        } data;
        char buf[sizeof(data)];
      } build;
      union Vison 
      {
        struct __attribute__((packed)) Data
        {
          unsigned short u;
        } data;
        char buf[sizeof(data)];
      } vision;
      union Ally
      {
        struct __attribute__((packed)) Data
        {
          unsigned int u;
        } data;
        char buf[sizeof(data)];
      } ally;
      union Hotkey
      {
        struct __attribute__((packed)) Data
        {
          /* 0x00 assign
           * 0x01 select
           * */
          unsigned char type;
          /* 0-9 */
          unsigned char number;
        } data;
        char buf[sizeof(data)];
      } hotkey;
      union Move
      {
        struct __attribute__((packed)) Data
        {
          unsigned short x;
          unsigned short y;
          /* 0xfff for using position x/y, or move to specific unit */
          unsigned short unitid;
          unsigned short u1;
          unsigned char u2;
        } data;
        char buf[sizeof(data)];
      } move;
      union Action
      {
        struct __attribute__((packed)) Data
        {
          unsigned short x;
          unsigned short y;
          /* 0xfff for using position x/y, or move to specific unit */
          unsigned short unitid;
          unsigned short u1;
          unsigned char type;
          unsigned char with_shift;
        } data;
        char buf[sizeof(data)];
      } action;
      union Cancel
      {
        struct __attribute__((packed)) Data
        {
        } data;
        char buf[sizeof(data)];
      } cancel;
      union CancelHatch
      {
        struct __attribute__((packed)) Data
        {
        } data;
        char buf[sizeof(data)];
      } cancelhatch;
      union Stop
      {
        struct __attribute__((packed)) Data
        {
          unsigned char u1;
        } data;
        char buf[sizeof(data)];
      } stop;
      union ReturnCargo
      {
        struct __attribute__((packed)) Data
        {
          unsigned char u1;
        } data;
        char buf[sizeof(data)];
      } returncargo;
      union Train
      {
        struct __attribute__((packed)) Data
        {
          unsigned short unit_type;
        } data;
        char buf[sizeof(data)];
      } train;
      union CancelTrain
      {
        struct __attribute__((packed)) Data
        {
          unsigned short unit_type;
        } data;
        char buf[sizeof(data)];
      } canceltrain;
      union Cloak
      {
        struct __attribute__((packed)) Data
        {
          unsigned char u1;
        } data;
        char buf[sizeof(data)];
      } cloak;
      union Decloak
      {
        struct __attribute__((packed)) Data
        {
          unsigned char u1;
        } data;
        char buf[sizeof(data)];
      } decloak;
      union Hatch
      {
        struct __attribute__((packed)) Data
        {
          unsigned short unit_type;
        } data;
        char buf[sizeof(data)];
      } hatch;
      union Unsiege
      {
        struct __attribute__((packed)) Data
        {
          unsigned char u1;
        } data;
        char buf[sizeof(data)];
      } unsiege;
      union Siege
      {
        struct __attribute__((packed)) Data
        {
          unsigned char u1;
        } data;
        char buf[sizeof(data)];
      } siege;
      union Scarab
      {
        struct __attribute__((packed)) Data
        {
        } data;
        char buf[sizeof(data)];
      } scarab;
      union UnloadAll
      {
        struct __attribute__((packed)) Data
        {
          unsigned char u1;
        } data;
        char buf[sizeof(data)];
      } unloadall;
      union Unload
      {
        struct __attribute__((packed)) Data
        {
          unsigned short u1;
        } data;
        char buf[sizeof(data)];
      } unload;
      union MergeArchon
      {
        struct __attribute__((packed)) Data
        {
        } data;
        char buf[sizeof(data)];
      } mergearchon;
      union HoldPosition
      {
        struct __attribute__((packed)) Data
        {
          unsigned char u1;
        } data;
        char buf[sizeof(data)];
      } holdposition;
      union Burrow
      {
        struct __attribute__((packed)) Data
        {
          unsigned char u1;
        } data;
        char buf[sizeof(data)];
      } burrow;
      union UnBurrow
      {
        struct __attribute__((packed)) Data
        {
          unsigned char u1;
        } data;
        char buf[sizeof(data)];
      } unburrow;
      union CancelNuke
      {
        struct __attribute__((packed)) Data
        {
        } data;
        char buf[sizeof(data)];
      } cancelnuke;
      union Lift
      {
        struct __attribute__((packed)) Data
        {
          unsigned int u1;
        } data;
        char buf[sizeof(data)];
      } lift;
      union Research
      {
        struct __attribute__((packed)) Data
        {
          unsigned char research_id;
        } data;
        char buf[sizeof(data)];
      } research;
      union CancelResearch
      {
        struct __attribute__((packed)) Data
        {
        } data;
        char buf[sizeof(data)];
      } cancelresearch;
      union Upgrade
      {
        struct __attribute__((packed)) Data
        {
          unsigned char upgrade_id;
        } data;
        char buf[sizeof(data)];
      } upgrade;
      union Morph
      {
        struct __attribute__((packed)) Data
        {
          unsigned short build_unit_type;
        } data;
        char buf[sizeof(data)];
      } morph;
      union Stim
      {
        struct __attribute__((packed)) Data
        {
        } data;
        char buf[sizeof(data)];
      } stim;
      union LeaveGame
      {
        struct __attribute__((packed)) Data
        {
          /* 0x01 quit
           * 0x06 drop
           * */
          unsigned char reason;
        } data;
        char buf[sizeof(data)];
      } leavegame;
      union MergeDarkArchon
      {
        struct __attribute__((packed)) Data
        {
        } data;
        char buf[sizeof(data)];
      } mergedarkarchon;
    } detail;
  };
};

void DumpCommandSize()
{
  printf("size(%s)=%ld\n", "Select", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Select));
  printf("size(%s)=%ld\n", "ShiftSelect", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::ShiftSelect));
  printf("size(%s)=%ld\n", "ShiftDelect", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::ShiftDelect));
  printf("size(%s)=%ld\n", "Build", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Build));
  printf("size(%s)=%ld\n", "Vison ", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Vison ));
  printf("size(%s)=%ld\n", "Ally", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Ally));
  printf("size(%s)=%ld\n", "Hotkey", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Hotkey));
  printf("size(%s)=%ld\n", "Move", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Move));
  printf("size(%s)=%ld\n", "Action", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Action));
  printf("size(%s)=%ld\n", "Cancel", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Cancel));
  printf("size(%s)=%ld\n", "CancelHatch", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::CancelHatch));
  printf("size(%s)=%ld\n", "Stop", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Stop));
  printf("size(%s)=%ld\n", "ReturnCargo", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::ReturnCargo));
  printf("size(%s)=%ld\n", "Train", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Train));
  printf("size(%s)=%ld\n", "CancelTrain", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::CancelTrain));
  printf("size(%s)=%ld\n", "Cloak", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Cloak));
  printf("size(%s)=%ld\n", "Decloak", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Decloak));
  printf("size(%s)=%ld\n", "Hatch", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Hatch));
  printf("size(%s)=%ld\n", "Unsiege", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Unsiege));
  printf("size(%s)=%ld\n", "Siege", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Siege));
  printf("size(%s)=%ld\n", "Scarab", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Scarab));
  printf("size(%s)=%ld\n", "UnloadAll", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::UnloadAll));
  printf("size(%s)=%ld\n", "Unload", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Unload));
  printf("size(%s)=%ld\n", "MergeArchon", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::MergeArchon));
  printf("size(%s)=%ld\n", "HoldPosition", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::HoldPosition));
  printf("size(%s)=%ld\n", "Burrow", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Burrow));
  printf("size(%s)=%ld\n", "UnBurrow", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::UnBurrow));
  printf("size(%s)=%ld\n", "CancelNuke", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::CancelNuke));
  printf("size(%s)=%ld\n", "Lift", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Lift));
  printf("size(%s)=%ld\n", "Research", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Research));
  printf("size(%s)=%ld\n", "CancelResearch", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::CancelResearch));
  printf("size(%s)=%ld\n", "Upgrade", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Upgrade));
  printf("size(%s)=%ld\n", "Morph", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Morph));
  printf("size(%s)=%ld\n", "Stim", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::Stim));
  printf("size(%s)=%ld\n", "LeaveGame", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::LeaveGame));
  printf("size(%s)=%ld\n", "MergeDarkArchon", 2+sizeof(udon::scr::benchmark::PlayerCommand::Command::Detail::MergeDarkArchon));
}

void DumpReplay(const char* loghd, const Replay& replay)
{
  printf("%sreplayid: %s\n", loghd, replay.replayid.c_str());
  printf("%su: %u\n", loghd, replay.u);
  printf("%sreplay.headersize: %ld\n", loghd, sizeof(replay.header.data));
  printf("%sengine: %d\n", loghd, replay.header.data.engine);
  printf("%sgame_frames: %d\n", loghd, replay.header.data.game_frames);
  printf("%ssavetime: %u\n", loghd, replay.header.data.save_time);
  printf("%sgame_name: %s\n", loghd, replay.header.data.game_name);
  printf("%smap_width: %u\n", loghd, replay.header.data.map_width);
  printf("%smap_height: %u\n", loghd, replay.header.data.map_height);
  printf("%screator: %s\n", loghd, replay.header.data.creator);
  printf("%smap_name: %s\n", loghd, replay.header.data.map_name);
}

int ParseReplayid(const char* data, int size, Replay* replay)
{
  int ret = 0;
  Chunk chunk = {};
  ret = ParseChunk(data, size, &chunk);
  if (ret < 0)
  {
    return ret;
  }

  if (chunk.raw.size() != 4)
  {
    return -6;
  }

  replay->replayid = std::move(chunk.raw);
  return ret;
}

int ParseHeader(const char* data, int size, Replay* replay)
{
  int ret = 0;
  Chunk chunk = {};
  ret = ParseChunk(data, size, &chunk);
  if (ret < 0)
  {
    return ret;
  }

  if (chunk.raw.size() != 0x279)
  {
    return -6;
  }
  memcpy(replay->header.buf, chunk.raw.data(), chunk.raw.size());
  return ret;
}

int ParsePlayerCommands(const char* data, int size, Replay* replay)
{
  int ret = 0;
  int read_len = 0;
  Chunk chunk = {};
  ret = ParseChunk(data, size, &chunk);
  if (ret < 0)
  {
    return ret;
  }
  read_len += ret;
  if (chunk.raw.size() != 4)
  {
    return -6;
  }
  Chunk::Len len;
  memcpy(len.buf, chunk.raw.data(), chunk.raw.size());

  chunk = {};
  ret = ParseChunk(data+read_len, size-read_len, &chunk);
  if (ret < 0)
  {
    return ret;
  }
  read_len += ret;
  if (chunk.raw.size() != len.data)
  {
    return -6;
  }
  // TODO
  return read_len;
}

int ParseMapData(const char* data, int size, Replay* replay)
{
  int ret = 0;
  int read_len = 0;
  Chunk chunk = {};
  ret = ParseChunk(data, size, &chunk);
  if (ret < 0)
  {
    return ret;
  }
  read_len += ret;
  if (chunk.raw.size() != 4)
  {
    return -6;
  }
  Chunk::Len len;
  memcpy(len.buf, chunk.raw.data(), chunk.raw.size());

  chunk = {};
  ret = ParseChunk(data+read_len, size-read_len, &chunk);
  if (ret < 0)
  {
    return ret;
  }
  read_len += ret;
  if (chunk.raw.size() != len.data)
  {
    return -6;
  }
  // TODO
  return read_len;
}

int ParseRwaData(const char* data, int size, Replay* replay)
{
  int ret = 0;
  int read_len = 0;
  Chunk chunk = {};
  ret = ParseChunk(data, size, &chunk);
  if (ret < 0)
  {
    return ret;
  }
  read_len += ret;
  if (chunk.raw.size() != 4)
  {
    return -6;
  }
  Chunk::Len len;
  memcpy(len.buf, chunk.raw.data(), chunk.raw.size());

  chunk = {};
  ret = ParseChunk(data+read_len, size-read_len, &chunk);
  if (ret < 0)
  {
    return ret;
  }
  read_len += ret;
  if (chunk.raw.size() != len.data)
  {
    return -6;
  }
  // TODO
  return read_len;
}

int ParseGap(const char* data, int size, Replay* replay)
{
  if (size < sizeof(replay->u))
  {
    return -1;
  }
  memcpy(&replay->u, data, sizeof(replay->u));
  return sizeof(replay->u);
}

int Parse(const char* data, int size, Replay* replay)
{
  int ret = 0;
  std::vector<int(*)(const char*, int, Replay*)> parse_funcs = 
  {
    &ParseReplayid,
    &ParseGap,
    &ParseHeader,
    &ParsePlayerCommands,
    &ParseMapData,
    // &ParseRwaData,
  };

  for (auto func: parse_funcs)
  {
    ret = (*func)(data, size, replay);
    if (ret < 0)
    {
      return ret;
    }
    // fprintf(stderr, "parsed: %d\n", ret);
    data += ret;
    size -= ret;
  }

  return 0;
}

}
}
}

int main(int argc, char** argv)
{
  int ret = 0;
  const char* path = argv[1];

  if (argc < 2)
  {
    fprintf(stderr, "%s <replay file>\n", argv[0]);
    return 1;
  }

  DumpCommandSize();

  std::string rep;
  ret = udon::Fd(path, O_RDONLY).Load(&rep);
  if (ret != 0)
  {
    fprintf(stderr, "ERR:%d: Load(%s) failed\n", ret, path);
    return ret;
  }

  udon::scr::benchmark::Replay replay;
  ret = Parse(rep.data(), rep.size(), &replay);
  if (ret != 0)
  {
    return ret;
  }

  DumpReplay("", replay);
  return 0;
}
