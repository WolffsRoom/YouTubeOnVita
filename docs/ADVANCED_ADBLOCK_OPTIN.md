# Experimental opt-in advanced AdBlock for YouTube on Vita

This pull request is submitted separately from [SponsorBlock](https://github.com/WolffsRoom/YouTubeOnVita/pull/7). **SponsorBlock skips community-submitted segments of a video; AdBlock filters ad responses. They are different functions.**

## What is added

A Cobalt 25 source patch that inserts an early YouTube TV document-only script. The script attempts to remove known YouTube advertisement response fields and ad renderer entries while preserving the media stream and playback metadata.

The existing upstream **AdBlock (partial)** setting controls the native, conservative DNS host filter. This PR also makes that existing persisted preference the **opt-in switch** for the JavaScript layer at browser startup:

- `adblock=0` or missing `addons.cfg`: custom Cobalt preload **not executed** (upstream default is OFF).
- `adblock=1`: when the YouTube TV document is created, custom Cobalt executes the filter before normal YouTube scripts.
- **Restart required to apply changes to the Cobalt layer.** The native DNS toggle itself can take effect immediately.

The patch reads only `ux0:data/youtubevita/addons.cfg` through the standard Starboard file API. No new custom Cobalt-to-native ABI is introduced. The fork-only `youtubevita-noads` directory and fork Title IDs are intentionally **not** included.

## Dependencies and builds

- Normal upstream VPK builds **must continue using the official, unchanged `libcobalt.so` bytes**.
- This source patch is **not effective with the unmodified binary**. An experimental Cobalt runtime must be compiled from source, and only explicitly opted-in packages should use it.
- The separate [optional source-built Cobalt loader/profile PR](https://github.com/WolffsRoom/YouTubeOnVita/pull/9) provides the required loader compatibility path. That PR is not merged; do not claim this filter currently ships in upstream VPKs.
- This PR deliberately does **not** change the default app version, packaged runtime, loader, account/session storage, SponsorBlock, or existing DNS filter implementation.

## Validation and limitations

Run `node tools/test_cobalt_adblock.cjs` and `node tools/test_advanced_adblock_source.cjs` for synthetic source-level tests. To validate end-to-end, build a modified `libcobalt.so` against the pinned Cobalt 25 source, generate a matching loader profile, package a test-only VPK, and test on an actual PS Vita.

Test both options off and on, restart between changes, compare advertisement playback in multiple video/Shorts contexts, monitor stream compatibility, and preserve redacted logs. **No claim of 100% ad blocking or hardware validation is made.** YouTube can change its server responses and bypass known ad-field filters.

The PR remains a draft until maintainers validate the source-built binary and complete hardware tests.
