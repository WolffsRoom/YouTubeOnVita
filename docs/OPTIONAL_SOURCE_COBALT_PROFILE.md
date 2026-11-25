# Experimental source-built Cobalt profile — optional

This proposal does **not** change the default YouTubeOnVita VPK: it still uses the **official, unmodified `libcobalt.so`** already selected by upstream CMake. Existing default runtime bytes, app IDs and version stay unchanged.

## Motivation
The Vita loader uses exact ARM ELF relocation counts, fixed offsets and import checks tailored to the official Cobalt 25 binary. A newly source-built Cobalt can differ at those offsets even when compiled from the official source.

## Opt-in workflow
1. Run the manual GitHub Actions workflow **Experimental source-built Cobalt loader profile**. It clones a pinned **unmodified** Cobalt 25 source revision, builds it via official toolchain containers, then generates a profile from the exact resulting ELF and its matching content directory.
2. Download the artifact `experimental-source-cobalt-vita-loader-profile-NOT-VPK`. It contains `libcobalt.so`, `content/`, a SHA-256 manifest, the generated `cobalt_candidate_profile.h`, and ELF diagnostics.
3. A developer can explicitly configure a Vita build using `-DYTV_CUSTOM_COBALT_DIR=/path/to/candidate`. The loader will validate the SHA-256 and metadata before executing the experimental binary. **This opt-in is not intended for regular users.**
4. The normal build (without that option) still uses the official Cobalt binary and its existing loader path. Run `node tools/test_optional_cobalt_default.cjs` for the static regression checks.

## Limitations
- The manual workflow is intentionally **not** triggered by pushes or pull requests; it is resource intensive.
- The integrated fork's experimental source-built Cobalt was built and tested on physical PS Vita hardware. **This independently prepared upstream-source workflow and its exact resulting ELF have not yet been compiled and boot-tested in isolation**; results from the fork do not guarantee that a different ELF is loadable. Fail-closed validation may reject unsupported layouts or imports.
- The official/default `libcobalt.so` is kept byte-identical; **the source-built experimental runtime is not claimed to match it byte-for-byte**.
- No filter, advertising-blocking or SponsorBlock modifications are included.

Only merge after maintainer review of the loader/profile contracts and device tests.