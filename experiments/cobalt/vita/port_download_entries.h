#pragma once
#include <cstddef>
#include <cstdint>

// Read-only snapshot for the Vita Add-ons download browser.
// state: 1 active, 2 queued, 3 saved MP3/MP4.
struct YtvDownloadEntry {
  char name[128];
  char detail[64];
  int state;
};
// Enumerates active/queued jobs in memory, then scans saved MP3/MP4 files.
// Call from the main input thread when the browser is opened/refreshed,
// never from the GPU frame callback.
extern "C" int ytv_native_download_entries(YtvDownloadEntry* items,int capacity);
extern "C" unsigned ytv_native_download_revision();
