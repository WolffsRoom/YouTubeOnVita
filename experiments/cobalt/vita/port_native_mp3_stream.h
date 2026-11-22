#pragma once
#include "port_download_metadata.h"
#include <cstdint>
#include <vector>

// Receive YouTube's AAC-only adaptive MP4 stream over verified HTTPS, decode
// incrementally with FFmpeg, and encode MP3 directly to a .part file.
// No intermediate MP4/m4a file. A false result leaves no usable output.
// Caller may retry the existing progressive MP4 -> MP3 fallback.
bool ytv_stream_audio_to_mp3(const char* source_url, const char* output_part,
                             const YtvVideoMetadata& info,
                             const std::vector<uint8_t>& cover,
                             volatile int* cancel,
                             void (*progress)(int percent),
                             uint64_t expected_audio_bytes);
