#include <cstdio>
#include <cstring>
#include <string>
#include <cstdlib>
#include <memory>
#include <functional>
#include <stdint.h>
#include <vector>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

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
    static const int READ_SIZE = 8*1024;
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

class Replay
{
 public:
  enum Errno
  {
    ERR_RWADATA_COMPRESSED_LEN = -9,
    ERR_RWADATA_LEN = -8,
    ERR_MAPDATA_COMPRESSED_LEN = -7,
    ERR_MAPDATA_LEN = -6,
    ERR_PLAYERSCOMMANDS_COMPRESSED_LEN = -5,
    ERR_PLAYERSCOMMANDS_LEN = -4,
    ERR_HEADER = -3,
    ERR_REPLAYID = -2,
    ERR_FORMAT = -1,
  };

 public:
  Replay();

  virtual ~Replay();

  int Parse(const char* data, int size);

  void Dump()
  {
    printf("replayid: %s\n", replayid_.c_str());
    printf("headersize: %ld\n", sizeof(header_.data));
    printf("engine: %d\n", header_.data.engine);
    printf("game_frames: %d\n", header_.data.game_frames);
    printf("savetime: %u\n", header_.data.save_time);
    printf("game_name: %s\n", header_.data.game_name);
    printf("map_width: %u\n", header_.data.map_width);
    printf("map_height: %u\n", header_.data.map_height);
    printf("creator: %s\n", header_.data.creator);
    printf("map_name: %s\n", header_.data.map_name);
  }

 public:
  int ParseReplayid(const char* data, int size);

  int ParseHeader(const char* data, int size);

  int ParsePlayerCommands(const char* data, int size);

  int ParseMapData(const char* data, int size);

  int ParseRwaData(const char* data, int size);

 protected:
  static const int REPLAY_HEADER_LEN = 0x279;
  static const int INT_LEN = sizeof(int);

 protected:
  std::string replayid_;
  union
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
    char buf[REPLAY_HEADER_LEN];
  }
  header_;

  union Len
  {
    int data;
    char buf[4];
  };
};

Replay::Replay()
{
  header_ = {};
}

Replay::~Replay()
{
}

int Replay::ParseReplayid(const char* data, int size)
{
  static const int NULL_HEAD_LEN = 12;
  static const int REPLAYID_LEN = 4;
  static const char* REPLAYID_SERS = "seRS";

  int ret = 0;

  if (size < NULL_HEAD_LEN+REPLAYID_LEN)
  {
    return ERR_FORMAT;
  }

  if (strncmp(data+NULL_HEAD_LEN, REPLAYID_SERS, REPLAYID_LEN) != 0)
  {
    return ERR_REPLAYID;
  }

  Len len = {};
  memcpy(len.buf, data+NULL_HEAD_LEN, INT_LEN);
  printf("0x%x\n", len.data);
  replayid_.assign(REPLAYID_SERS);
  return NULL_HEAD_LEN+REPLAYID_LEN;
}

int Replay::ParseHeader(const char* data, int size)
{
  int ret = 0;
  if (size < REPLAY_HEADER_LEN)
  {
    return ERR_HEADER;
  }

  memcpy(header_.buf, data, REPLAY_HEADER_LEN);
  return REPLAY_HEADER_LEN;
}

int Replay::Parse(const char* data, int size)
{
  int ret = 0;
  int parsedlen = 0;
  std::vector<int(Replay::*)(const char*, int)> parse_funcs = 
  {
    &Replay::ParseReplayid,
    &Replay::ParseHeader,
    &Replay::ParsePlayerCommands,
    &Replay::ParseMapData,
    &Replay::ParseRwaData,
  };

  for (auto func: parse_funcs)
  {
    ret = (this->*func)(data, size);
    if (ret < 0)
    {
      return ret;
    }
    printf("parsed: %d\n", ret);
    data += ret;
    size -= ret;
  }

  return size;
}

int Replay::ParsePlayerCommands(const char* data, int size)
{
  int ret = 0;
  if (size < INT_LEN)
  {
    return ERR_PLAYERSCOMMANDS_LEN;
  }

  Len len = {};
  memcpy(len.buf, data, INT_LEN);
  printf("%s: %d\n", __func__, len.data);

  if (size < INT_LEN+len.data)
  {
    return ERR_PLAYERSCOMMANDS_COMPRESSED_LEN;
  }

  return INT_LEN+len.data;
}

int Replay::ParseMapData(const char* data, int size)
{
  int ret = 0;
  if (size < INT_LEN)
  {
    return ERR_MAPDATA_LEN;
  }

  Len len = {};
  memcpy(len.buf, data, INT_LEN);
  printf("%s: %d\n", __func__, len.data);

  if (size < INT_LEN+len.data)
  {
    return ERR_MAPDATA_COMPRESSED_LEN;
  }

  return INT_LEN+len.data;
}

int Replay::ParseRwaData(const char* data, int size)
{
  int ret = 0;
  if (size < INT_LEN)
  {
    return ERR_RWADATA_LEN;
  }

  Len len = {};
  memcpy(len.buf, data, INT_LEN);
  printf("%s: %d\n", __func__, len.data);

  if (size < INT_LEN+len.data)
  {
    return ERR_RWADATA_COMPRESSED_LEN;
  }

  return INT_LEN+len.data;
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

  std::string rep;
  ret = udon::Fd(path, O_RDONLY).Load(&rep);
  if (ret != 0)
  {
    fprintf(stderr, "ERR:%d: Load(%s) failed\n", ret, path);
    return ret;
  }

  udon::scr::benchmark::Replay replay;
  ret = replay.Parse(rep.data(), rep.size());
  // if (ret != 0)
  // {
  //   return ret;
  // }

  replay.Dump();
  return 0;
}
