from pathlib import Path
root=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=root/'tools/update_cobalt_gn_overlay.py'
s=p.read_text(encoding='utf-8-sig')
key="'port_storage.cpp','import_lookup.cpp']"
replacement=("'port_storage.cpp','port_system.cpp','port_events.cpp',\n"
             "'port_constants.cpp','port_optional_unsupported.cpp',\n"
             "'port_media_policy.cpp','import_lookup.cpp']")
assert key in s
s=s.replace(key,replacement)
p.write_text(s,encoding='utf-8')
print('NEW_NATIVE_STARBOARD_FILES_INCLUDED_IN_GN_OVERLAY')

