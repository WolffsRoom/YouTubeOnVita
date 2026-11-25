# Native MediaSession Skipad action

This is an independent, reviewable AdBlock helper. It **does not remove video ads from YouTube responses**. When the native AdBlock preference is enabled and the YouTube TV frontend exposes the MediaSession `Skipad` action, it schedules that action on the Starboard event loop and requests a skip.

This is not SponsorBlock (community-submitted video-segment skipping). It does not change the default official Cobalt binary, native DNS settings, account data or the existing UI.

- Default native AdBlock stays **off**.
- Only schedules when the platform reports `kCobaltExtensionMediaSessionActionSkipad` available.
- Avoids invoking Cobalt synchronously from the update callback; uses `SbEventSchedule` and a pending-generation guard.
- Cancels stale pending events on callback destruction/re-registration and when Skipad is no longer available.
- Disables any pending action when the AdBlock setting is switched off before dispatch.

**Important:** the user-visible feature needs hardware testing with videos that actually expose a Skip action. It cannot prevent ads that provide no `Skipad` action and does **not** guarantee all ads disappear. If SponsorBlock PR #7 merges first, this file may require a small rebase because both PRs touch the MediaSession bridge. Keep this contribution separate from the source-Cobalt response filter PR #10.
