// Source-level guard: filter must be opt-in, read the existing Vita menu
// setting, and never alter the official default libcobalt.so.
const fs = require('node:fs');
const assert = require('node:assert/strict');
const script = fs.readFileSync('tools/prepare_cobalt_adblock.py','utf8');
const filter = fs.readFileSync('tools/cobalt_adblock_preload.js','utf8');
const cmake = fs.readFileSync('CMakeLists.txt','utf8');
assert.ok(script.includes('YTV_OPTIONAL_ADBLOCK_PRELOAD'));
assert.ok(script.includes('SbFileOpen("ux0:data/youtubevita/addons.cfg"'));
assert.ok(script.includes('enable_ad_filter = false;'));
assert.ok(script.includes('entry[8] == \'1\''));
assert.ok(script.includes('if (enable_ad_filter)'));
assert.ok(script.includes('data.initial_url.path() == "/tv"'));
assert.ok(script.includes('kSbFileOpenOnly | kSbFileRead'));
assert.ok(filter.includes('adPlacements'));
assert.ok(filter.includes('playerAds'));
assert.ok(filter.includes('streamingData'));
assert.ok(filter.includes('__ytvAdFilter'));
assert.ok(cmake.includes('experiments/cobalt/evergreen/core25/lib/libcobalt.so'));
assert.ok(!cmake.includes('YTV_CUSTOM_COBALT_DIR'));
console.log('PASS: opt-in Cobalt preload, exact add-on path, no default binary changes');
