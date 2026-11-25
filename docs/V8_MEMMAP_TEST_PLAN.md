# Experimental V8 data mapping consistency — hardware evidence needed

**Status: unproven hypothesis / draft for review, not a confirmed crash fix.**

## What the existing Vita port does
- `SbMemoryMap(Read)` requests real RO protection on a new kernel memblock.
- `SbMemoryProtect(ReadWrite)` accepts certain data-page transitions without necessarily changing the hardware protection.
- The proposed change leaves non-executable data mappings RW from creation, consistent with the latter behavior; it does **not** change how executable/JIT pages are handled.

## Real-device testing context

The integrated fork has been exercised on a physical PS Vita, and the reported DFSR=7 dumps come from actual hardware. These tests are relevant history, **not** a controlled same-binary A/B proof of this particular fix. The isolated upstream patch still requires that A/B check.

## Observations and limitations
Two partial coredump excerpts previously shared with the fork showed `DFSR=7` translation faults on a read. That does **not** prove the fault was caused by protection flags. The excerpts are not full dumps with symbolized stacks, and this PR has not passed controlled hardware A/B tests.

**Tradeoff:** keeping mappings writable relaxes effective RO protection and may conceal other bugs. This is a reason not to merge without review and evidence.

## A/B validation request before merge
1. Use the **same official `libcobalt.so` bytes**, loader configuration, title ID, device and media quality for the baseline and patched builds.
2. Establish a crash reproduction case on PS Vita (including playback duration, video resolution, login state and plugins present).
3. Run baseline and patched versions across multiple comparable extended playback sessions, recording crash occurrence and uptime.
4. Save `cobalt_boot.log` and `starboard15.log` locally, and compare `DFSR`/`DFAR` and symbolized stacks for any failures. Treat coredumps as sensitive; only share redacted metadata publicly.
5. Confirm no regressions in JIT execution, memory mapping/unmapping, account sign-in, 720p and lower-resolution playback.

Do **not** promote this as a resolved crash until hardware evidence supports it.
