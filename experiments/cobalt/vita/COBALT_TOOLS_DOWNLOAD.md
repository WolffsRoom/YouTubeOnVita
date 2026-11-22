# YouTubeVita — Cobalt Tools native downloader (experimental)

Date: 2026-11-18. This is Cobalt Tools (imputnet/cobalt), not Google's Cobalt browser used for the genuine YouTube TV site.

## Status
- Added **Download video / Baixar vídeo** in the native Select → Add-ons overlay. X/touch starts; selecting again requests cancellation.
- Worker thread does not stall YouTube TV UI. Streaming write to storage avoids keeping whole file in RAM.
- Native VitaSDK libcurl with validated CA certificates, HTTPS-only API and transfer, JSON POST /, optional Api-Key and tunnel/redirect response support.
- MP4 + H.264: 360p, 480p (default), 720p. Audio-only: MP3 (subject to API output).
- Writes to ux0:data/youtubevita/downloads/VIDEOID_QUALITY.mp4 or VIDEOID_audio.mp3. Uses .part and removes partial files on errors/cancel; never replaces existing files.
- **Limitation:** cannot automatically extract the video currently highlighted in the Google Cobalt browser. For now, the URL is supplied via VitaShell/FTP.
- **Not validated:** actual download using Cobalt Tools API, Vita3K UI behavior and real PS Vita hardware. Compilation/package checks alone do not constitute a functional release.

## Configuration

Create ux0:data/youtubevita/download.cfg (UTF-8):
~~~ini
api=https://YOUR-OWN-OR-AUTHORIZED-COBALT-INSTANCE/
key=
quality=480
mode=video
~~~

key= is optional and is an API key assigned by the instance owner. quality can be 360, 480, 720; mode can be video or audio. This file is never packaged in the VPK.

Create ux0:data/youtubevita/download_request.txt:
~~~text
https://www.youtube.com/watch?v=XXXXXXXXXXX
~~~

Also accepts https://youtu.be/XXXXXXXXXXX and mobile YouTube watch URLs. Only individual videos, not playlists. Save media you own or are authorized to download.

On the Vita: SELECT → Add-ons → Baixar vídeo → X. Selecting the same item while transferring requests cancellation. Progress appears beside the menu item. Completed files are in ux0:data/youtubevita/downloads/.

## Next step

The public api.cobalt.tools server is bot-protected and not approved for use by arbitrary third-party integrations. Self-host or obtain authorization. The API must serve files over HTTPS and supply a TLS-valid certificate. Conversion/remuxing is performed by the API, not by the Vita.

To download the currently playing video without a manually provided URL, a supported bridge from the Google Cobalt YouTube TV runtime to the native add-on layer is still necessary. Do not rely on guesses from thumbnails or encrypted traffic.

Docs: https://github.com/imputnet/cobalt/blob/main/docs/api.md
Experimental build only — do not replace the current release until tested.
