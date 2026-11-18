from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=r/'experiments/cobalt/vita/relocate_elf.c'
s=p.read_text(encoding='utf-8-sig')
old='''  int f=sceIoOpen("ux0:data/youtubevita/relocations.log",'''
new='''#if defined(YTV_COBALT_BOOT_STAGE)
  int f=sceIoOpen("ux0:data/youtubevita/cobalt_boot.log",
#else
  int f=sceIoOpen("ux0:data/youtubevita/relocations.log",
#endif'''
assert old in s
s=s.replace(old,new,1)
anchor='int main(void) {'
assert anchor in s
helper=r'''
static int get_init_array(uint32_t *addr,uint32_t *length){
  if(!addr||!length)return 0;
  *addr=0;*length=0;
  for(unsigned i=0;i<nheaders;i++){
    const Elf32_Phdr* p=&headers[i];
    if(p->p_type!=PT_DYNAMIC)continue;
    if(!in_load(p->p_vaddr,p->p_memsz))return 0;
    const Elf32_Dyn* d=(const Elf32_Dyn*)(image+p->p_vaddr);
    for(unsigned j=0;j<p->p_memsz/sizeof(*d);j++){
      if(d[j].d_tag==DT_INIT_ARRAY)*addr=d[j].d_un.d_ptr;
      else if(d[j].d_tag==DT_INIT_ARRAYSZ)*length=d[j].d_un.d_val;
      else if(d[j].d_tag==DT_NULL)break;
    }
  }
  return *addr && *length && (*length%4)==0 &&
         in_load(*addr,*length);
}
'''
s=s.replace(anchor,helper+'\n'+anchor,1)
anchor='''      log_line("COBALT_CODE_EXECUTION_NOT_ATTEMPTED");'''
assert anchor in s
new=r'''      uint32_t ctor_addr=0,ctor_bytes=0;
      int ctor_valid=get_init_array(&ctor_addr,&ctor_bytes) &&
                     ctor_bytes==74u*sizeof(uint32_t);
      if(ctor_valid){
        uint32_t* ctors=(uint32_t*)(image+ctor_addr);
        for(unsigned j=0;j<ctor_bytes/4;j++){
          uint32_t ptr=ctors[j] & ~1u;
          if(ptr<base32+xstart || ptr>=base32+xstart+xlen){
            ctor_valid=0;break;
          }
        }
      }
      note_u32("ELF_INIT_ARRAY",ctor_bytes/4,ctor_valid?1u:0u);
      check(ctor_valid,"all 74 Cobalt C++ initializers belong to RX text");
      if(failures)goto release;
#if defined(YTV_COBALT_BOOT_STAGE) && (YTV_COBALT_BOOT_STAGE>=1)
      if(still_missing!=0)goto release;
      typedef void (*Ctor)(void);
      uint32_t *initializer=(uint32_t*)(image+ctor_addr);
      log_line("COBALT_CONSTRUCTOR_PROBE_BEGIN");
      for(unsigned j=0;j<ctor_bytes/4;j++){
        char item[80];
        snprintf(item,sizeof(item),"COBALT_CTOR_ENTER %u",j);
        log_line(item);
        Ctor call=(Ctor)(uintptr_t)initializer[j];
        call();
        snprintf(item,sizeof(item),"COBALT_CTOR_RETURN %u",j);
        log_line(item);
      }
      log_line("COBALT_CONSTRUCTORS_COMPLETE");
#else
      log_line("COBALT_CODE_EXECUTION_NOT_ATTEMPTED");
#endif'''
s=s.replace(anchor,new,1)
p.write_text(s,encoding='utf-8')
cm=r/'CMakeLists.txt';s=cm.read_text(encoding='utf-8-sig')
if 'YTV_BUILD_COBALT_BOOT_PROBE' not in s:
 appended='''
# Stage 17: isolated Cobalt initializer execution, NOT a playable app.
option(YTV_BUILD_COBALT_BOOT_PROBE "Build isolated native Cobalt init probe" ON)
if(YTV_BUILD_COBALT_BOOT_PROBE)
  set(VITA_APP_NAME "Cobalt Constructor Probe")
  set(VITA_TITLEID "YTVCB0001")
  set(VITA_VERSION "00.17")
  add_executable(ytv_cobalt_boot experiments/cobalt/vita/relocate_elf.c)
  target_compile_definitions(ytv_cobalt_boot PRIVATE YTV_COBALT_BOOT_STAGE=1)
  target_compile_options(ytv_cobalt_boot PRIVATE -O2 -Wall -Wextra
    -mcpu=cortex-a9 -mfpu=neon -mfloat-abi=hard -fno-short-enums)
  target_link_libraries(ytv_cobalt_boot PRIVATE starboard_vita15 kubridge_stub
    vitaGL vitashark SceShaccCgExt SceShaccCg_stub taihen_stub
    SceDisplay_stub SceGxm_stub SceAudio_stub SceKernelDmacMgr_stub
    ScePower_stub SceAppMgr_stub SceCtrl_stub SceTouch_stub
    SceCommonDialog_stub SceIofilemgr_stub SceLibKernel_stub
    SceSysmem_stub SceNet_stub SceNetCtl_stub SceSysmodule_stub SceAppUtil_stub
    SceKernelThreadMgr_stub mathneon pthread z m)
  vita_create_self(ytv_cobalt_boot.self ytv_cobalt_boot UNSAFE)
  vita_create_vpk(CobaltNativeBootstrapTest.vpk @@{VITA_TITLEID} ytv_cobalt_boot.self
    VERSION @@{VITA_VERSION} NAME @@{VITA_APP_NAME}
    FILE legacy/native-mock-v001/sce_sys/icon0.png sce_sys/icon0.png
    FILE @@{COBALT_CORE} libcobalt.so)
endif()
'''
 s+=appended.replace('@@',chr(36))
 cm.write_text(s,encoding='utf-8')
print('CTOR_VALIDATION_AND_ISOLATED_PROBE_ADDED')

