#pragma once
#include <cstddef>
#include <cstdint>

// Minimal forward-only fragmented MP4 audio parser. No libavformat MOV
// demuxer is present in VitaSDK's FFmpeg build.
using YtvMp4StreamRead = int (*)(void*,uint8_t*,int);
using YtvMp4StreamInit = bool (*)(void*,const uint8_t*,int,int,int);
using YtvMp4StreamSample = bool (*)(void*,const uint8_t*,int);
bool ytv_mp4_stream_extract_aac(void* opaque,YtvMp4StreamRead read,
                                YtvMp4StreamInit init,YtvMp4StreamSample sample);
