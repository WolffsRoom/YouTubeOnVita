from pathlib import Path
root=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
src=root/'experiments/cobalt/vita'
dst=src/'gn_overlay/starboard/vita/armv7'
names=['platform15.cpp','port_window.cpp','pad_adapter.cpp','pad_adapter.h','port_files.cpp',
'port_sync.cpp','port_network.cpp','port_socket_waiter.cpp','port_runtime.cpp',
'port_threads.cpp','port_graphics.cpp','port_virtual_memory.cpp',
'port_storage.cpp','port_system.cpp','port_events.cpp',
'port_constants.cpp','port_optional_unsupported.cpp',
'port_media_policy.cpp','import_lookup.cpp']
for name in names:
 assert (src/name).is_file(),name
 (dst/name).write_bytes((src/name).read_bytes())
p=dst/'BUILD.gn'
files=',\n'.join('    "%s"'%name for name in names if name.endswith('.cpp'))
p.write_text('''# Partial native Vita Starboard 15 implementation, NOT Cobalt production yet.
# Final app must link vitaGL/vitashark, VitaSDK kernel/net libraries, pthread,
# and kubridge. This platform is not sufficient to run all Cobalt exports.
static_library("starboard_platform") {
  check_includes = false
  sources = [
%s,
  ]
  configs += [ "//starboard/build/config:starboard_implementation" ]
}
'''%files,encoding='utf-8')
print('GN_VITA_OVERLAY_UPDATED_WITH',len(names),'source/header files')

