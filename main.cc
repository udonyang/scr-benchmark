#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <cstdarg>

#include <utility>
#include <sstream>
#include <string>
#include <memory>
#include <functional>
#include <vector>
#include <unordered_map>

#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <zlib.h>

namespace scr
{

std::string&& FmtStr(const char* fmt, ...)
{
  va_list va;
  va_start(va, fmt);
  int len = vfprintf(stderr, fmt, va);
  std::string foo(len, '\0');
  vsnprintf((char*)foo.data(), len, fmt, va);
  va_end(va);
  return std::move(foo);
}

int LoadFile(const char* path, std::string* data)
{
  int ret = 0;
  int fd = open(path, O_RDONLY);
  if (fd < 0)
  {
    return -1;
  }
  std::shared_ptr<int> _fd(&fd, [](int* fd){close(*fd);});

  struct stat path_stat;
  ret = fstat(fd, &path_stat);
  if (ret != 0)
  {
    return -3;
  }

  data->resize(path_stat.st_size);
  ret = read(fd, (void*)data->data(), path_stat.st_size);
  if (ret != path_stat.st_size)
  {
    return -2;
  }

  return 0;
}

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
      std::shared_ptr<z_stream> _zstream(&zstream, [](z_stream* zstream){inflateEnd(zstream);});

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
          return -5;
        }
        
        chunk->raw.append(buf, BUFSIZ-zstream.avail_out);
        // fprintf(stderr, "have=%d avalid_out=%d\n", BUFSIZ-zstream.avail_out, zstream.avail_out);
        if (ret == Z_STREAM_END)
        {
          break;
        }
      }
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

struct Frame
{
  struct __attribute__((packed)) Time
  {
    unsigned int pasted;
    unsigned char command_len;
  } time;

  struct __attribute__((packed)) CommandHead
  {
    unsigned char playerid;
    unsigned char cmdid;
  };

  struct __attribute__((packed)) Command
  {
    CommandHead head;
    unsigned int detail[0];
  };
  std::vector<std::shared_ptr<Command>> command;

  struct __attribute__((packed)) KeepAlive
  {
    CommandHead head;
  };

  struct __attribute__((packed)) SaveGame
  {
    CommandHead head;
    unsigned int count;
    char u1[1<<(sizeof(char)<<3)];
  };

  struct __attribute__((packed)) LoadGame
  {
    CommandHead head;
    unsigned int count;
  };

  struct __attribute__((packed)) RestartGame
  {
    CommandHead head;
  };

  struct __attribute__((packed)) Select
  {
    CommandHead head;
    unsigned char unit_cnt;
    unsigned short unit[1<<(sizeof(unit_cnt)<<3)];
  };

  struct __attribute__((packed)) ShiftSelect
  {
    CommandHead head;

    unsigned char unit_cnt;
    unsigned short unit[1<<(sizeof(unit_cnt)<<3)];
  };

  struct __attribute__((packed)) ShiftDelect
  {
    CommandHead head;

    unsigned char unit_cnt;
    unsigned short unit[1<<(sizeof(unit_cnt)<<3)];
  };

  struct __attribute__((packed)) Build
  {
    CommandHead head;

    unsigned char build_unit_type;
    unsigned short x;
    unsigned short y;
    unsigned short build_unitid;
  };

  struct __attribute__((packed)) Vison
  {
    CommandHead head;

    unsigned short u;
  };

  struct __attribute__((packed)) Ally
  {
    CommandHead head;

    unsigned int u;
  };

  struct __attribute__((packed)) GameSpeed
  {
    CommandHead head;
    unsigned char speed;
  };

  struct __attribute__((packed)) Pause
  {
    CommandHead head;
  };

  struct __attribute__((packed)) Resume
  {
    CommandHead head;
  };

  struct __attribute__((packed)) Cheat
  {
    CommandHead head;
    char u1[4];
  };


  struct __attribute__((packed)) Hotkey
  {
    CommandHead head;

    /* 0x00 assign
     * 0x01 select
     * */
    unsigned char type;
    /* 0-9 */
    unsigned char number;
  };

  struct __attribute__((packed)) Move
  {
    CommandHead head;

    unsigned short x;
    unsigned short y;
    /* 0xfff for using position x/y, or move to specific unit */
    unsigned short unitid;
    unsigned short u1;
    unsigned char u2;
  };

  struct __attribute__((packed)) Action
  {
    CommandHead head;

    unsigned short x;
    unsigned short y;
    /* 0xfff for using position x/y, or move to specific unit */
    unsigned short unitid;
    unsigned short u1;
    unsigned char type;
    unsigned char with_shift;
  };

  struct __attribute__((packed)) Cancel
  {
    CommandHead head;
  };

  struct __attribute__((packed)) CancelHatch
  {
    CommandHead head;
  };

  struct __attribute__((packed)) Stop
  {
    CommandHead head;

    unsigned char u1;
  };

  struct __attribute__((packed)) CarrierStop
  {
    CommandHead head;
  };

  struct __attribute__((packed)) ReaverStop
  {
    CommandHead head;
  };

  struct __attribute__((packed)) OrderNothing
  {
    CommandHead head;
  };

  struct __attribute__((packed)) ReturnCargo
  {
    CommandHead head;

    unsigned char u1;
  };

  struct __attribute__((packed)) Train
  {
    CommandHead head;

    unsigned short unit_type;
  };

  struct __attribute__((packed)) CancelTrain
  {
    CommandHead head;

    unsigned short unit_type;
  };

  struct __attribute__((packed)) Cloak
  {
    CommandHead head;

    unsigned char u1;
  };

  struct __attribute__((packed)) Decloak
  {
    CommandHead head;

    unsigned char u1;
  };

  struct __attribute__((packed)) Hatch
  {
    CommandHead head;

    unsigned short unit_type;
  };

  struct __attribute__((packed)) Unsiege
  {
    CommandHead head;

    unsigned char u1;
  };

  struct __attribute__((packed)) Siege
  {
    CommandHead head;

    unsigned char u1;
  };

  struct __attribute__((packed)) Scarab
  {
    CommandHead head;

  };

  struct __attribute__((packed)) UnloadAll
  {
    CommandHead head;

    unsigned char u1;
  };

  struct __attribute__((packed)) Unload
  {
    CommandHead head;

    unsigned short u1;
  };

  struct __attribute__((packed)) MergeArchon
  {
    CommandHead head;

  };

  struct __attribute__((packed)) HoldPosition
  {
    CommandHead head;

    unsigned char u1;
  };

  struct __attribute__((packed)) Burrow
  {
    CommandHead head;

    unsigned char u1;
  };

  struct __attribute__((packed)) UnBurrow
  {
    CommandHead head;

    unsigned char u1;
  };

  struct __attribute__((packed)) CancelNuke
  {
    CommandHead head;

  };

  struct __attribute__((packed)) Lift
  {
    CommandHead head;

    unsigned short x;
    unsigned short y;
  };

  struct __attribute__((packed)) Research
  {
    CommandHead head;

    unsigned char research_id;
  };

  struct __attribute__((packed)) CancelResearch
  {
    CommandHead head;

  };

  struct __attribute__((packed)) Upgrade
  {
    CommandHead head;

    unsigned char upgrade_id;
  };

  struct __attribute__((packed)) CancelUpgrade
  {
    CommandHead head;
  };

  struct __attribute__((packed)) CancelAddon
  {
    CommandHead head;
  };

  struct __attribute__((packed)) Morph
  {
    CommandHead head;

    unsigned short build_unit_type;
  };

  struct __attribute__((packed)) Stim
  {
    CommandHead head;

  };

  struct __attribute__((packed)) Sync
  {
    CommandHead head;
    char u1[6];
  };

  struct __attribute__((packed)) VoiceEnable
  {
    CommandHead head;
  };

  struct __attribute__((packed)) VoiceDisable
  {
    CommandHead head;
  };

  struct __attribute__((packed)) VoiceSquelch
  {
    CommandHead head;
    char u1[1];
  };

  struct __attribute__((packed)) VoiceUnsquelch
  {
    CommandHead head;
    char u1[1];
  };

  struct __attribute__((packed)) StartGame
  {
    CommandHead head;
  };

  struct __attribute__((packed)) DownloadPercentage
  {
    CommandHead head;
    char u1[1];
  };

  struct __attribute__((packed)) ChangeGameSlot
  {
    CommandHead head;
    char u1[5];
  };

  struct __attribute__((packed)) NewNetPlayer
  {
    CommandHead head;
    char u1[7];
  };

  struct __attribute__((packed)) JoinedGame
  {
    CommandHead head;
    char u1[17];
  };

  struct __attribute__((packed)) ChangeRace
  {
    CommandHead head;
    char u1[2];
  };

  struct __attribute__((packed)) TeamGameTeam
  {
    CommandHead head;
    char u1[1];
  };

  struct __attribute__((packed)) UMSTeam
  {
    CommandHead head;
    char u1[1];
  };

  struct __attribute__((packed)) MeleeTeam
  {
    CommandHead head;
    char u1[2];
  };

  struct __attribute__((packed)) SwapPlayers
  {
    CommandHead head;
    char u1[2];
  };

  struct __attribute__((packed)) SavedData
  {
    CommandHead head;
    char u1[12];
  };

  struct __attribute__((packed)) BriefingStart
  {
    CommandHead head;
  };

  struct __attribute__((packed)) Latency
  {
    CommandHead head;
    unsigned char type;
  };

  struct __attribute__((packed)) ReplaySpeed
  {
    CommandHead head;
    unsigned char u1[9];
  };

  struct __attribute__((packed)) LeaveGame
  {
    CommandHead head;

    /* 0x01 quit
     * 0x06 drop
     * */
    unsigned char reason;
  };

  struct __attribute__((packed)) MinimapPing
  {
    CommandHead head;
    unsigned short x;
    unsigned short y;
  };

  struct __attribute__((packed)) MergeDarkArchon
  {
    CommandHead head;

  };

  struct __attribute__((packed)) MakeGamePublic
  {
    CommandHead head;
  };

  struct __attribute__((packed)) Chat
  {
    CommandHead head;
    unsigned char playerid;
    char msg[80];
  };

  struct __attribute__((packed)) Select121
  {
    CommandHead head;
    unsigned char nunit;
    struct __attribute__((packed)) UnitInfo
    {
      unsigned short unit_type;
      unsigned short u1;
    };
    UnitInfo units[1<<(sizeof(nunit)<<3)];
  };

};
static const std::unordered_map<unsigned char, std::pair<const char*, int>> g_cmd_info = 
{
  {0x05,  {"KeepAlive",           sizeof(Frame::KeepAlive)           }},
  {0x06,  {"SaveGame",            sizeof(Frame::SaveGame)            }},
  {0x07,  {"LoadGame",            sizeof(Frame::LoadGame)            }},
  {0x08,  {"RestartGame",         sizeof(Frame::RestartGame)         }},
  {0x09,  {"Select",              sizeof(Frame::Select)              }},
  {0x0A,  {"ShiftSelect",         sizeof(Frame::ShiftSelect)         }},
  {0x0B,  {"ShiftDelect",         sizeof(Frame::ShiftDelect)         }},
  {0x0C,  {"Build",               sizeof(Frame::Build)               }},
  {0x0D,  {"Vison",               sizeof(Frame::Vison)               }},
  {0x0E,  {"Ally",                sizeof(Frame::Ally)                }},
  {0x0f,  {"GameSpeed",           sizeof(Frame::GameSpeed)           }},
  {0x10,  {"Pause",               sizeof(Frame::Pause)               }},
  {0x11,  {"Resume",              sizeof(Frame::Resume)              }},
  {0x12,  {"Cheat",               sizeof(Frame::Cheat)               }},
  {0x13,  {"Hotkey",              sizeof(Frame::Hotkey)              }},
  {0x14,  {"Move",                sizeof(Frame::Move)                }},
  {0x15,  {"Action",              sizeof(Frame::Action)              }},
  {0x18,  {"Cancel",              sizeof(Frame::Cancel)              }},
  {0x19,  {"CancelHatch",         sizeof(Frame::CancelHatch)         }},
  {0x1A,  {"Stop",                sizeof(Frame::Stop)                }},
  {0x1b,  {"CarrierStop",         sizeof(Frame::CarrierStop)         }},
  {0x1c,  {"ReaverStop",          sizeof(Frame::ReaverStop)          }},
  {0x1d,  {"OrderNothing",        sizeof(Frame::OrderNothing)        }},
  {0x1E,  {"ReturnCargo",         sizeof(Frame::ReturnCargo)         }},
  {0x1F,  {"Train",               sizeof(Frame::Train)               }},
  {0x20,  {"CancelTrain",         sizeof(Frame::CancelTrain)         }},
  {0x21,  {"Cloak",               sizeof(Frame::Cloak)               }},
  {0x22,  {"Decloak",             sizeof(Frame::Decloak)             }},
  {0x23,  {"Hatch",               sizeof(Frame::Hatch)               }},
  {0x25,  {"Unsiege",             sizeof(Frame::Unsiege)             }},
  {0x26,  {"Siege",               sizeof(Frame::Siege)               }},
  {0x27,  {"Scarab",              sizeof(Frame::Scarab)              }},
  {0x28,  {"UnloadAll",           sizeof(Frame::UnloadAll)           }},
  {0x29,  {"Unload",              sizeof(Frame::Unload)              }},
  {0x2A,  {"MergeArchon",         sizeof(Frame::MergeArchon)         }},
  {0x2B,  {"HoldPosition",        sizeof(Frame::HoldPosition)        }},
  {0x2C,  {"Burrow",              sizeof(Frame::Burrow)              }},
  {0x2D,  {"UnBurrow",            sizeof(Frame::UnBurrow)            }},
  {0x2E,  {"CancelNuke",          sizeof(Frame::CancelNuke)          }},
  {0x2F,  {"Lift",                sizeof(Frame::Lift)                }},
  {0x30,  {"Research",            sizeof(Frame::Research)            }},
  {0x31,  {"CancelResearch",      sizeof(Frame::CancelResearch)      }},
  {0x32,  {"Upgrade",             sizeof(Frame::Upgrade)             }},
  {0x33,  {"CancelUpgrade",       sizeof(Frame::CancelUpgrade)       }},
  {0x34,  {"CancelAddon",         sizeof(Frame::CancelAddon)         }},
  {0x35,  {"Morph",               sizeof(Frame::Morph)               }},
  {0x36,  {"Stim",                sizeof(Frame::Stim)                }},
  {0x37,  {"Sync",                sizeof(Frame::Sync)                }},
  {0x38,  {"VoiceEnable",         sizeof(Frame::VoiceEnable)         }},
  {0x39,  {"VoiceDisable",        sizeof(Frame::VoiceDisable)        }},
  {0x3a,  {"VoiceSquelch",        sizeof(Frame::VoiceSquelch)        }},
  {0x3b,  {"VoiceUnsquelch",      sizeof(Frame::VoiceUnsquelch)      }},
  {0x3c,  {"StartGame",           sizeof(Frame::StartGame)           }},
  {0x3d,  {"DownloadPercentage",  sizeof(Frame::DownloadPercentage)  }},
  {0x3e,  {"ChangeGameSlot",      sizeof(Frame::ChangeGameSlot)      }},
  {0x3f,  {"NewNetPlayer",        sizeof(Frame::NewNetPlayer)        }},
  {0x40,  {"JoinedGame",          sizeof(Frame::JoinedGame)          }},
  {0x41,  {"ChangeRace",          sizeof(Frame::ChangeRace)          }},
  {0x42,  {"TeamGameTeam",        sizeof(Frame::TeamGameTeam)        }},
  {0x43,  {"UMSTeam",             sizeof(Frame::UMSTeam)             }},
  {0x44,  {"MeleeTeam",           sizeof(Frame::MeleeTeam)           }},
  {0x45,  {"SwapPlayers",         sizeof(Frame::SwapPlayers)         }},
  {0x48,  {"SavedData",           sizeof(Frame::SavedData)           }},
  {0x54,  {"BriefingStart",       sizeof(Frame::BriefingStart)       }},
  {0x55,  {"Latency",             sizeof(Frame::Latency)             }},
  {0x56,  {"ReplaySpeed",         sizeof(Frame::ReplaySpeed)         }},
  {0x57,  {"LeaveGame",           sizeof(Frame::LeaveGame)           }},
  {0x58,  {"MinimapPing",         sizeof(Frame::MinimapPing)         }},
  {0x5A,  {"MergeDarkArchon",     sizeof(Frame::MergeDarkArchon)     }},
  {0x5b,  {"MakeGamePublic",      sizeof(Frame::MakeGamePublic)      }},
  {0x5c,  {"Chat",                sizeof(Frame::Chat)                }},
  {0x63,  {"Select121",           sizeof(Frame::Select121)           }},
};

void DumpCmdInfo()
{
  for (const auto& info: g_cmd_info)
  {
    printf("0x%02hhX, %s, %d\n", info.first, info.second.first, info.second.second);
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
  };
  Header header;
  std::vector<Frame> frames;
};

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
  for (int i = 0; i < replay.frames.size(); i++)
  {
    printf("%sframe[%d].time.pasted: %u\n", loghd, i, replay.frames[i].time.pasted);
    printf("%sframe[%d].time.command_len: %hhu\n", loghd, i, replay.frames[i].time.command_len);
  }
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

int Forward(bool lookahead_only, const char* src, int nsrc, int nread, int ndst, void* dst)
{
  if (nsrc < nread+ndst)
  {
    return -6;
  }
  memcpy(dst, src+nread, ndst);
  if (lookahead_only)
  {
    return nread;
  }
  return nread+ndst;
}

int Forward(const char* src, int nsrc, int nread, int ndst, void* dst)
{
  return Forward(false, src, nsrc, nread, ndst, dst);
}

int Lookahead(const char* src, int nsrc, int nread, int ndst, void* dst)
{
  return Forward(true, src, nsrc, nread, ndst, dst);
}

int ParseCommand(const char* data, int size, Replay* replay)
{
  int ret = 0;
  int read_len = 0;

  while (read_len < size)
  {
    Frame frame = {};
    read_len = Forward(data, size, read_len, sizeof(frame.time), &frame.time);
    if (read_len < 0) 
    {
      return read_len;
    }

    fprintf(stderr, "frame.time: {pasted: %u, command_len: %hhu}\n", frame.time.pasted, frame.time.command_len);

    for (int i = 0; i < frame.time.command_len; i++)
    {
      Frame::CommandHead head = {};
      read_len = Lookahead(data, size, read_len, sizeof(head), &head);
      if (read_len < 0)
      {
        return read_len;
      }

      fprintf(stderr, "head: { playerid: %hhu, cmdid: 0x%hhX\n", head.playerid, head.cmdid);

      auto cmd_info = g_cmd_info.find(head.cmdid);
      if (cmd_info == g_cmd_info.end())
      {
        fprintf(stderr, "unknown cmd[0x%hhx]\n", head.cmdid);
        return -6;
      }

      int ncmd = cmd_info->second.second;
      std::shared_ptr<Frame::Command> cmd((Frame::Command*)operator new(ncmd));
      read_len = Forward(data, size, read_len, ncmd, cmd.get());
      if (read_len < 0)
      {
        return read_len;
      }
    }
    replay->frames.push_back(std::move(frame));
  }
  return 0;
}

int ParseFrame(const char* data, int size, Replay* replay)
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

  ret = ParseCommand(chunk.raw.data(), chunk.raw.size(), replay);
  if (ret != len.data)
  {
    return ret;
  }

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
    &ParseFrame,
    // &ParseMapData,
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

int main(int argc, char** argv)
{
  int ret = 0;
  const char* path = argv[1];

  if (argc < 2)
  {
    fprintf(stderr, "%s <replay file>\n", argv[0]);
    return 1;
  }

  // scr::DumpCmdInfo();

  std::string rep;
  ret = scr::LoadFile(path, &rep);
  if (ret != 0)
  {
    fprintf(stderr, "ERR:%d: Load(%s) failed\n", ret, path);
    return ret;
  }

  scr::Replay replay;
  ret = scr::Parse(rep.data(), rep.size(), &replay);
  if (ret != 0)
  {
    return ret;
  }

  DumpReplay("", replay);
  return 0;
}
