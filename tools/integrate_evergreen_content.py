from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=r/'CMakeLists.txt'
s=p.read_text(encoding='utf-8-sig')
d=chr(36)
old='''  vita_create_vpk(CobaltNativeBootstrapTest.vpk @ {VITA_TITLEID} ytv_cobalt_boot.self
    VERSION @ {VITA_VERSION} NAME @ {VITA_APP_NAME}
    FILE legacy/native-mock-v001/sce_sys/icon0.png sce_sys/icon0.png
    FILE @ {COBALT_CORE} libcobalt.so)
'''.replace('@ ',d)
new='''  # Package only content actually extracted from the matching official CRX.
  set(COBALT_EXTRACTED_CONTENT_DIR "@ {CMAKE_CURRENT_SOURCE_DIR}/experiments/cobalt/evergreen/core25/content")
  file(GLOB_RECURSE COBALT_BOOT_ASSETS CONFIGURE_DEPENDS
       "@ {COBALT_EXTRACTED_CONTENT_DIR}/*")
  set(COBALT_BOOT_ASSET_FILES "")
  foreach(COBALT_BOOT_ASSET IN LISTS COBALT_BOOT_ASSETS)
    file(RELATIVE_PATH COBALT_BOOT_ASSET_DEST
      "@ {COBALT_EXTRACTED_CONTENT_DIR}" "@ {COBALT_BOOT_ASSET}")
    list(APPEND COBALT_BOOT_ASSET_FILES
      FILE "@ {COBALT_BOOT_ASSET}" "@ {COBALT_BOOT_ASSET_DEST}")
  endforeach()
  vita_create_vpk(CobaltNativeBootstrapTest.vpk @ {VITA_TITLEID} ytv_cobalt_boot.self
    VERSION @ {VITA_VERSION} NAME @ {VITA_APP_NAME}
    FILE legacy/native-mock-v001/sce_sys/icon0.png sce_sys/icon0.png
    FILE @ {COBALT_CORE} libcobalt.so
    @ {COBALT_BOOT_ASSET_FILES})
'''.replace('@ ',d)
assert s.count(old)==1
p.write_text(s.replace(old,new),encoding='utf-8')
print('OFFICIAL_COBALT_CONTENT_VPK_STAGE_CONFIGURED')

