# YouTubeVita — Native Player Tools & Settings (v0.39 prototype)

Date: 2026-11-18

## Implemented

- SELECT opens the native YouTubeVita hub (Player tools / Settings / Close), usable from any screen.
- TRIANGLE opens the native Player tools context overlay while a Starboard SbPlayer session exists. TRIANGLE from a non-player screen falls through to the real YouTube TV app. CIRCLE closes the player context if opened directly from Triangle; if reached from the hub, CIRCLE returns to the hub.
- Player tools: download, screenshot, sleep timer, screen off/audio on, Back.
- Settings: partial DNS-only AdBlock, download quality 360/480/720, download format MP4/MP3, L/R button toggle, lock input, UI language System/PT-BR/English, Back.
- D-pad UP/DOWN navigates; LEFT/RIGHT changes adjustable values; X confirms; O returns; SELECT closes. Front touch taps use dynamic panel-height coordinates.
- Existing screenshot, screen-off, input lock, timer, and download functionality preserved.
- Native settings persist in ux0:data/youtubevita/addons.cfg. Download quality and format override only the preferences in the existing download.cfg while keeping the user's API host/key untouched.
- Added native SbPlayer lifetime bridge to detect an existing session; no DOM modification or unverified Google Cobalt binary patch.

## Important limitations

- No HTML/JavaScript injection into the proprietary YouTube TV webpage. The overlays look integrated but are Vita-native and appear on top of the player, not inside the original webpage DOM.
- Triangle visibility is based on the existence of an SbPlayer session, which can include preview playback, not guaranteed fullscreen-only.
- **Download video still requires** ux0:data/youtubevita/download_request.txt containing a watch URL and download.cfg containing a permitted Cobalt Tools API endpoint/key. Automatic extraction of the selected video's ID is not yet available.
- AdBlock filters only an explicit list of known third-party ad hostnames at Starboard DNS resolution; ads delivered by first-party YouTube player streams are not blocked. No promise of ad-free playback.
- MP3 conversion depends on the Cobalt API. The server should own media conversion; the Vita streams the result to a .part file.
- A long video download, sleep/wake, and playback on real Vita hardware still require testing.

## Tests completed

- VitaSDK/Docker toolchain compiled all modified C++ units, linked the ARM Vita ELF, and built a VPK successfully.
- Vita3K 2026-11-18-172110: SELECT -> hub, DOWN -> Settings, quality changed to 720p, format changed to MP3. Menu navigation and persistence confirmed.
- Vita3K 2026-11-18-172259: launched video by CROSS, TRIANGLE opened Player native overlay during playback, CIRCLE closed it, without crash in capture.
- The screenshots and logs are in tools/capture/cobalt-boot-20261118-172110 and -172259.

## Follow-up engineering investigations

- Consider a measured, version-guarded JavaScript/native bridge to query the actual YouTube TV video ID (possibly window.ytInitialPlayerResponse.videoDetails.videoId). Do not inject into Cobalt Evergreen without observing it on the installed runtime and measuring memory/crash behavior.
- Investigate deeper player ad filtering independently; blocking youtube.com/googlevideo.com at DNS level would break normal playback.
- Confirm 1-minute settings usability and clean return on physical PS Vita; inspect screenshots in Portuguese.

No changes to the pre-existing current/YouTubeVita_v0.38.vpk.
