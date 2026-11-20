#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

struct YtvVideoMetadata {
  std::string title;
  std::string artist;
  std::string genre;
};

// Extract only the intended videoDetails / playerMicroformatRenderer scope.
// Handles JSON escaped Unicode, surrogate pairs and UTF-8.
bool ytv_parse_video_metadata(const char* response, YtvVideoMetadata& info);

// Returns a filename only (no directories). The actual video height is always
// the final suffix on MP4: "Video Title - 360p.mp4".
std::string ytv_download_filename(const YtvVideoMetadata& info,
                                  const char* video_id, int height, bool audio);

// Create a bounded ID3v2.3 tag using UTF-16 text frames and optional JPEG APIC.
// The JPEG must be a real, reasonably-sized image fetched over verified HTTPS.
std::vector<uint8_t> ytv_make_mp3_id3(const YtvVideoMetadata& info,
                                      const std::vector<uint8_t>& jpeg);
