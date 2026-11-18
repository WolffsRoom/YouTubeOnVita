from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
cm=r/'CMakeLists.txt'
s=cm.read_text(encoding='utf-8')
assert 'port_graphics.cpp")' in s
s=s.replace('port_graphics.cpp")','port_graphics.cpp" "'+chr(36)+'{VITA}/port_virtual_memory.cpp")',1)
old='''SceIofilemgr_stub SceLibKernel_stub SceKernelThreadMgr_stub SceNet_stub SceSysmodule_stub pthread m)'''
new='''SceIofilemgr_stub SceLibKernel_stub SceKernelThreadMgr_stub
      SceNet_stub SceSysmodule_stub SceSysmem_stub kubridge_stub pthread m)'''
assert old in s
s=s.replace(old,new,1)
cm.write_text(s,encoding='utf-8')
p=r/'experiments/cobalt/vita/import_lookup.cpp'
s=p.read_text(encoding='utf-8')
assert '    {"SbLog",' in s
add=['SbMemoryMap','SbMemoryUnmap','SbMemoryProtect']
s=s.replace('    {"SbLog",',''.join('    {"%s", reinterpret_cast<uintptr_t>(&%s)},\n'%(n,n) for n in add)+'    {"SbLog",')
p.write_text(s,encoding='utf-8')
p=r/'experiments/cobalt/vita/main_test.cpp'
s=p.read_text(encoding='utf-8')
marker='  char result[96];'
assert marker in s
block=r'''  // Page-based Vita user-memory API with real kernel permissions.
  void* region=SbMemoryMap(65536,kSbMemoryMapProtectReadWrite,"YTVM");
  check(region!=SB_MEMORY_MAP_FAILED && region!=nullptr &&
        (reinterpret_cast<uintptr_t>(region)&4095)==0,
        "Starboard allocates 64KB of 4KB-aligned Vita virtual memory");
  if(region!=SB_MEMORY_MAP_FAILED && region) {
    static_cast<unsigned char*>(region)[0]=0x5C;
    static_cast<unsigned char*>(region)[4096]=0xA6;
    check(SbMemoryProtect(region,4096,kSbMemoryMapProtectRead),
          "Starboard protects first 4KB virtual page read-only");
    check(static_cast<unsigned char*>(region)[0]==0x5C &&
          static_cast<unsigned char*>(region)[4096]==0xA6,
          "Starboard memory protection preserves content of adjacent pages");
    check(SbMemoryProtect(region,4096,kSbMemoryMapProtectReadWrite),
          "Starboard safely restores page read/write permissions");
    check(!SbMemoryProtect(region,4096,kSbMemoryMapProtectReadWrite|
           kSbMemoryMapProtectExec),"Starboard forbids writable executable pages");
    check(!SbMemoryProtect(static_cast<unsigned char*>(region)+1,4096,
           kSbMemoryMapProtectRead),"Starboard forbids nonaligned protection range");
    check(!SbMemoryUnmap(region,32768),
          "Starboard partial unmap fails closed instead of corrupting allocation");
    check(SbMemoryUnmap(region,65536),"Starboard releases whole 64KB Vita memory block");
    check(!SbMemoryUnmap(region,65536),"Starboard does not free memory block twice");
  }
  check(SbMemoryMap(0,kSbMemoryMapProtectReadWrite,"BAD")==SB_MEMORY_MAP_FAILED,
        "Starboard rejects zero-sized map");
'''
s=s.replace(marker,block+marker)
p.write_text(s,encoding='utf-8')
p=r/'tools/test_starboard_vita3k.ps1'
s=p.read_text(encoding='utf-8')
s=s.replace('if($Passed -ne 66){throw "EXPECTED_STARBOARD_CHECK_COUNT 66, got $Passed"}',
            'if($Passed -ne 76){throw "EXPECTED_STARBOARD_CHECK_COUNT 76, got $Passed"}')
p.write_text(s,encoding='utf-8')
p=r/'experiments/cobalt/vita/relocate_elf.c'
s=p.read_text(encoding='utf-8')
s=s.replace('imported==86 &&','imported==89 &&')
s=s.replace('ytv_real_starboard_export_count()==88','ytv_real_starboard_export_count()==91')
p.write_text(s,encoding='utf-8')
p=r/'tools/test_relocations_vita3k.ps1'
s=p.read_text(encoding='utf-8')
s=s.replace('REAL_STARBOARD_EXPORTS_LINKED 86 125','REAL_STARBOARD_EXPORTS_LINKED 89 122')
s=s.replace('LINKED=86 MISSING=125','LINKED=89 MISSING=122')
p.write_text(s,encoding='utf-8')
print('VIRTUAL_MEMORY_API_REGRESSION_ADDED')

