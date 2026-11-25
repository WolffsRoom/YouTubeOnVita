// Static regression checks; PS Vita hardware validation is still required.
const fs=require('node:fs'),assert=require('node:assert/strict');
const media=fs.readFileSync('experiments/cobalt/vita/port_media_session.cpp','utf8');
const menu=fs.readFileSync('experiments/cobalt/vita/port_addons.cpp','utf8');
assert.ok(media.includes('g_ytv_sponsorblock_mode = 0'), 'SponsorBlock must default OFF');
assert.ok(media.includes('https://sponsor.ajay.app/api/skipSegments/'));
assert.ok(media.includes('SbEventSchedule(sb_action_on_main,nullptr,0)'));
assert.ok(media.includes('SbEventCancel(sb_action_event)'));
const cb=media.slice(media.indexOf('void sb_action_on_main'),media.indexOf('bool sb_schedule_action_locked'));
assert.ok(cb.indexOf('pthread_mutex_unlock(&sb_mutex)')<cb.indexOf('invoke(action,context)'),
          'must release lock before invoking Cobalt');
assert.ok(!/kSbSkipAd|YTV_ADBLOCK|g_ytv_adblock/.test(media),
          'no adblocking implementation in SponsorBlock MediaSession integration');
assert.ok(menu.includes('if (g_page == kPageSettings) return 9;'));
assert.ok(menu.includes('sponsorblock=%d'));
assert.ok(menu.includes('if (const char* v=strstr(buf,"sponsorblock="))'));
assert.ok(menu.includes('case 7:') && menu.includes('g_ytv_sponsorblock_mode,mode,__ATOMIC_RELAXED'));
console.log('SponsorBlock opt-in source checks passed. Hardware playback unverified.');
