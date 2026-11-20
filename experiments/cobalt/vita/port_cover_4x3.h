#pragma once
#include <cstdint>
#include <vector>

// Decode a visible YouTube artwork (JPEG/PNG/WebP), remove uniform black
// letterbox bars, crop to 4:3 and encode a 480x360 JPEG front-cover image.
// Bounded allocations; returns false for unsupported/corrupt input.
bool ytv_cover_normalize_4x3(const std::vector<uint8_t>& incoming,
                             std::vector<uint8_t>& jpeg_out);


// MP3 front-cover APIC: a square, center-cropped 480x480 JPEG, no distortion.
// Uses the same bounded JPEG/PNG/WebP decoder and bar removal.
bool ytv_cover_normalize_square(const std::vector<uint8_t>& incoming,
                                std::vector<uint8_t>& jpeg_out);
