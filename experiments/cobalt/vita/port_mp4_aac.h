#pragma once
#include <psp2/io/fcntl.h>
#include <cstdint>
#include <vector>

// Small read-only ISO BMFF/AAC sample-table parser for native MP3 encoding.
// Handles ordinary nonfragmented MP4 (mp4a/stsz/stsc/stco/co64); no DRM/fMP4.
struct YtvAacSample {
  uint64_t offset;
  uint32_t bytes;
};
struct YtvMp4Audio {
  int sample_rate = 0;
  int channels = 0;
  uint8_t asc[64] = {};
  int asc_bytes = 0;
  std::vector<YtvAacSample> samples;
  uint64_t file_bytes = 0;
};
bool ytv_mp4_parse_audio(SceUID file, YtvMp4Audio& out);
bool ytv_mp4_parse_audio_moov(const uint8_t* moov,size_t bytes,
                              uint64_t max_file_bytes,YtvMp4Audio& out);
