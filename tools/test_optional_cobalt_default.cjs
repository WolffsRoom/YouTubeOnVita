// Upstream must continue packaging the exact unmodified official libcobalt.so
// unless a developer *explicitly* opts in to a source-built runtime.
const assert=require('node:assert/strict'),fs=require('node:fs');
const cm=fs.readFileSync('CMakeLists.txt','utf8');
const loader=fs.readFileSync('experiments/cobalt/vita/relocate_elf.c','utf8');
assert.ok(cm.includes('set(YTV_CUSTOM_COBALT_DIR "" CACHE PATH'));
assert.ok(cm.includes('set(COBALT_CORE "${CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/evergreen/core25/lib/libcobalt.so")'));
assert.ok(cm.includes('if(YTV_CUSTOM_COBALT_DIR)'));
assert.ok(cm.includes('set(COBALT_CORE "${YTV_CUSTOM_COBALT_DIR}/libcobalt.so")'));
assert.ok(cm.includes('YTV_COBALT_DIAG_EVENT_CUTOFF=1'));
assert.ok(cm.includes('YTV_COBALT_APP_VERSION "00.63"'));
assert.ok(loader.includes('#define EXPECT_BYTES 33491716LL'));
assert.ok(loader.includes('#define EXPECT_FLUSH 0xd35198u'));
assert.ok(loader.includes('#if defined(YTV_CUSTOM_COBALT)'));
assert.ok(loader.includes('verify_candidate_sha(len)'));
const py=fs.readFileSync('tools/prepare_vita_cobalt_profile.py','utf8');
assert.ok(!py.includes('ytv-adblock-preload.js'));
assert.ok(!py.includes('cobalt_adblock_preload'));
console.log('Default still uses upstream libcobalt.so; alternative runtime is opt-in only.');
