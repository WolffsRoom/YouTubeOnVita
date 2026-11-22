# VitaBrowser: status (2026-11-18)

Goal: a web browser for PS Vita built on the YouTubeOnVita port (official
Cobalt Evergreen 25.lts.50.1035044 / 5.50.2, Starboard 15, our ELF loader).
Decision: Cobalt + native shell (not a source-built Cobalt, not NetSurf).

Paused here to return to YouTube on Vita. No separate project exists yet; all
code below lives in the YouTubeVita tree.

## What was verified (Vita3K only, nothing on hardware)

| Test | Result |
|---|---|
| Local page `file:///x.html` (app0:web/) with CSS + JS | Renders; JS runs |
| Local page without any CSP | Renders |
| `location.href = "https://…"` from a local page | Navigates (DNS + TLS + fetch) |
| `--url=https://example.com/` (no CSP header), unpatched | Document refused (`SbSystemRaisePlatformError` type 0) |
| `--url=https://en.wikipedia.org/…` (has CSP), unpatched | Loads, but black screen |
| Both, with the patches below | Render: white page, text, links, search button |
| Wikipedia's own site CSS | **Not applied** (page looks unstyled) |

## Root causes found

1. **Production Cobalt forces security flags** (`cobalt/browser/application.cc`,
   `COBALT_FORCE_CSP/HTTPS/CORS`): CSP required, HTTPS required, CORS
   required. `--csp_mode`, `--require_csp`, `--min_log_level` are ignored, and
   the gold build prints no Cobalt logs at all.
   - `HTMLDecoder` (`dom_parser/html_decoder.cc`) refuses a document without a
     `Content-Security-Policy` header ("Failure receiving Content Security
     Policy headers for URL").
   - `CspDelegateSecure::CanLoad` with no header received and policy
     "required" refuses every subresource (CSS, scripts, images) except the
     default navigation policy -> black page.
2. **Cobalt's UA style sheet is the TV one**: 773 bytes, only `display` rules,
   no background/colour/margins. Pages that rely on browser defaults (or use
   CSS Cobalt cannot parse, e.g. `light-dark()`) end up black on black.
3. **The platform cannot navigate Cobalt.** Deep links (`kSbEventTypeLink`)
   are only delivered to page JS (`h5vcc.runtime.onDeepLink`); Cobalt never
   navigates on them (`cobalt/doc/deep_links.md`). Only page JS can navigate.
   Note: `--proxy` *is* honoured in gold builds (outside the debug-switch
   block).

## Code changes (all in `experiments/cobalt/vita/relocate_elf.c`)

Applied right after relocation, before the text segment is made R-X. Each
patch first checks the original instruction words (version guard) and logs a
`note_u32` line in `cobalt_boot.log`.

Enabled when built with `YTV_BROWSER_BUILD`, or (YouTube build, diagnostics
only) when `ux0:data/youtubevita/csp_optional.txt` exists. The YouTube app is
unchanged without that file.

| Patch | Address (vaddr) | Original | New | Log line |
|---|---|---|---|---|
| CanLoad: drop "policy == required" return | `0x18239bc` | `beq` `0x0a000012` (after `e5961014` `e3510000`) | `nop` `0xe320f000` | `COBALT_CSP_OPTIONAL … 1` |
| HTMLDecoder: accept documents without CSP | `0x1b9cc00` | `beq` `0x0a000030` (after `e595000c` `e3500001`) | `b` `0xea000030` | `COBALT_HTML_CSP_OPTIONAL … 1` |
| UA style sheet override | `movw r3,#773` @ `0x19954f0` (`0xe3003305`), literal @ `0x19957ec` (`0xfee564b1`), `add r1,pc,r1` @ `0x19954fc` | Cobalt's 773-byte sheet @ `0x7eb9b5` | our sheet (pointer + size) | `COBALT_UA_SHEET <size> 1` |

UA sheet source: `experiments/cobalt/vita/browser/ua.css` (2628 bytes). Loaded
from `app0:browser/ua.css` in a browser build, `ux0:data/youtubevita/ua.css`
in the YouTube build (diagnostic).

Also added (kept for both apps): `SbSystemRaisePlatformError` now logs
`YTV_LIFECYCLE_PLATFORM_ERROR type=N` (port_misc_utility.cpp). Cobalt calls it
~1 s after a document load fails.

## How the patch sites were found

- libcobalt code is **ARM** (not Thumb). String references are
  `ldr rX,[pc,#imm]` + `add rY, pc, rX` (target = add + 8 + literal).
- Text vaddr = file offset + 0x10000; rodata vaddr = file offset.
- `tools/cobalt_re/xref_arm.py libcobalt.so 0xSTRING…` finds code using a
  string; `tools/cobalt_re/callers.py libcobalt.so 0xFUNC…` finds `bl`/`b`
  callers. Disassemble with
  `arm-vita-eabi-objdump -d --start-address=… --stop-address=… libcobalt.so`
  in the vitasdk container.
- Strings used: "Content-Security-Policy-Report-Only headers were received…"
  (`0x1be121`, CspDelegateSecure::OnReceiveHeaders @ `0x1823c58`),
  "Page must include Content-Security-Policy header…" (`0x1ccd3e`, CanLoad @
  `0x1823918`), csp::ResponseHeaders ctor @ `0x1957a90` (3 callers; the
  HTMLDecoder one is `0x1b9cbc8`), UA sheet text "The user agent style sheet
  is applied…" (`0x7eb9b5`).

## How to reproduce the tests

```
ux0:data/youtubevita/cobalt_args.txt   --url=https://example.com/
ux0:data/youtubevita/csp_optional.txt  (any content)
ux0:data/youtubevita/ua.css            copy of browser/ua.css
tools/test_cobalt_boot_vita3k.ps1 -Seconds 40 -CaptureFrames -CaptureIntervalMs 6000 -CaptureMax 7
```
Remove all three files afterwards: they affect the YouTube build.

## Open problems

1. **Site CSS not applied** (Wikipedia). Probable cause: Cobalt's CSS parser
   drops style sheets that use syntax it does not know (`@layer`, `:is()`,
   `light-dark()`, …). To test: copy the page and its CSS into a local
   `file://` page and remove constructs one by one.
2. **CORS required** is still forced (cross-origin XHR/fonts/images may fail).
   HTTPS-required means `http://` sites are refused. Not patched yet.
3. **Native navigation**: plan is a local start page (`app0:web/…`) that
   navigates via JS, plus a relaunch with `--url` (sceAppMgrLoadExec) for
   "Home"/"Go to URL" from a third-party page. Untested.
4. Text input: Cobalt's on-screen-keyboard Starboard API must be mapped to
   the Vita IME dialog. Not started.
5. Cobalt has no iframes / partial web platform: some sites will not work no
   matter what.

## Planned project shape (not started)

`SDKVita\VitaBrowser`: separate title ID, reuses the YouTubeVita port and
loader with `YTV_BROWSER_BUILD`; packages `browser/ua.css`, a start page with
address field (Vita IME) and favourites, and the native SELECT menu with
Home / Go to URL / Back.
