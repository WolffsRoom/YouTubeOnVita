# YouTubeVita v0.40 — Download Crash Fix (experimental)

Date: 2026-11-18.
Source of the hardware failure: logs-psv/v039 DOWNLOAD/

## Verified crash root cause on physical PS Vita

The gzip-compressed ELF core file psp2core-1795046681-0x00000d2bf9-eboot.bin.psp2dmp contains:
- Crashed thread UID 0x40070357, name pthread.
- Exception 0x30004 (ARM Data Abort), PC 0xe000ff46 (SceLibKernel), LR 0x8103f871 (app code).
- The app code return site resolves to run_download(), immediately after a memset in its prologue.
- THREAD_INFO contains a 0x8000 (32 KiB) thread stack, base 0x81c29000.
- The crash-time stack pointer was 0x81c27e88 and target of memset was 0x81c28408, both below the allocated stack base.
- Original run_download() compiled with ~0x912c (>37 KiB) frame, mostly Response resp containing char json[32768].
- This is a native worker stack overflow, not a GPU/video decoder fault.

## Fix

File: experiments/cobalt/vita/port_downloads.cpp
- Allocates the ~32 KiB Cobalt Tools API response buffer on heap via calloc() and releases it on all request completion/error paths.
- Native worker pthread explicitly uses 128 KiB stack via pthread_attr_setstacksize(), and checks thread-attribute creation errors.
- Keeps the existing download-in-progress and cancellation guards.
- Adds YTV_DOWNLOAD_REQUEST, YTV_DOWNLOAD_WORKER_ENTER/EXIT, YTV_DOWNLOAD_CONFIG_MISSING_OR_INVALID, YTV_DOWNLOAD_URL_MISSING_OR_INVALID, and YTV_DOWNLOAD_STATE diagnostic log lines; secrets and URLs are not logged.
- After rebuild, run_download() static frame decreases to roughly 4.4 KiB, confirmed in the ARM objdump.
- User interface unchanged from v0.39.

## Verification

1. Rebuilt native VitaSDK C++ modules, executable and VPK with Docker. Build succeeded.
2. Vita3K log tools/capture/cobalt-boot-20261118-211341/sb.log:
   - Video running, TRIANGLE opened native player menu, CROSS selected Download twice.
   - Each click logged YTV_DOWNLOAD_REQUEST, YTV_DOWNLOAD_WORKER_ENTER,
     YTV_DOWNLOAD_CONFIG_MISSING_OR_INVALID, YTV_DOWNLOAD_WORKER_EXIT.
   - No crash.
3. Vita3K log tools/capture/cobalt-boot-20261118-211742/sb.log:
   - With a temporary intentionally invalid API hostname and an example 11-character video ID,
     the test logged YTV_DOWNLOAD_STATE Cobalt API...,
     YTV_DOWNLOAD_STATE Cobalt API error, and YTV_DOWNLOAD_WORKER_EXIT.
   - No crash, and the player menu remained usable.

## Limitations

- This verifies the stack-overflow fix in Vita3K, not yet on a physical PS Vita. A hardware test with this build remains necessary.
- Automatic extraction of the active YouTube TV video ID is not yet implemented.
- An authorized Cobalt Tools instance in ux0:data/youtubevita/download.cfg and URL in download_request.txt are still required to perform a real download.
- The invalid test API was never capable of returning video; a successful real media download has NOT been validated.
- AdBlock remains DNS-only and partial.
- Existing current/YouTubeVita_v0.39.vpk, current/anteriores/YouTubeVita_v0.38.vpk and older prototype VPKs were left untouched.

## Output

output/native-ui-v040/YouTubeVita-v0.40-DownloadCrashFix-EXPERIMENTAL.vpk
