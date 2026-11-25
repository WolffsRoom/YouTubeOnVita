// Source-level callback safety checks; real PS Vita playback validation required.
const fs=require('node:fs'),assert=require('node:assert/strict');
const media=fs.readFileSync('experiments/cobalt/vita/port_media_session.cpp','utf8');
assert.ok(media.includes('extern "C" int g_ytv_adblock_enabled;'));
assert.ok(media.includes('state.available_actions[kCobaltExtensionMediaSessionActionSkipad]'));
assert.ok(media.includes('SbEventSchedule(skip_ad_on_event_loop,nullptr,0)'));
assert.ok(media.includes('SbEventCancel(skip_event)'));
const action=media.slice(media.indexOf('void skip_ad_on_event_loop'),media.indexOf('void maybe_schedule_skip_ad'));
assert.ok(action.indexOf('pthread_mutex_unlock(&skip_mutex)')<action.indexOf('invoke(details,context)'));
assert.ok(action.includes('g_ytv_adblock_enabled'));
assert.ok(media.includes('++skip_generation;'));
assert.ok(!/SponsorBlock|sponsorblock|youtubevita-noads/.test(media));
console.log('PASS: opt-in native Skipad event-loop dispatch, cancellation and no SponsorBlock coupling');
