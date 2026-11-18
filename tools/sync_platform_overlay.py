from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
overlay=r/'experiments/cobalt/vita/gn_overlay/starboard/vita/armv7'
src=r/'experiments/cobalt/vita'
files=['platform15.cpp','port_window.cpp','pad_adapter.cpp','pad_adapter.h',
       'port_files.cpp','port_sync.cpp','port_runtime.cpp','port_threads.cpp',
       'port_network.cpp','port_socket_waiter.cpp','port_graphics.cpp',
       'port_virtual_memory.cpp','import_lookup.cpp']
for filename in files:
  (overlay/filename).write_bytes((src/filename).read_bytes())
sources=',\n    '.join('"'+name+'"' for name in files if name.endswith('.cpp'))
build='''# Genuine native Starboard Vita API implementations (not full Cobalt).
# Prerequisites: VitaSDK, vitaGL, vitashark, kubridge and SceNet libraries.
# GN depends on a complete upstream Cobalt/Chromium dependency checkout.
static_library("starboard_platform") {
  check_includes = false
  sources = [
    '''+sources+'''
  ]
  configs += [ "//starboard/build/config:starboard_implementation" ]
}
'''
(overlay/'BUILD.gn').write_text(build,encoding='utf-8')
print('GN_SOURCE_FILES_SYNCED',len(files),'BUILD_CPP',len([name for name in files if name.endswith('.cpp')]))

