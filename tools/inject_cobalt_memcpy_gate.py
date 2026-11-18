from pathlib import Path
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita\relocate_elf.c')
s=p.read_text(encoding='utf-8-sig')
anchor='''      log_line("COBALT_CONSTRUCTOR_PROBE_BEGIN");'''
assert anchor in s
new=r'''      log_line("COBALT_CONSTRUCTOR_PROBE_BEGIN");
      // Probe an official Cobalt internal ARM memcpy function which ctor #0
      // calls through PLT. This checks actual cross-page execution and return.
      {
        typedef void (*ArmCopy)(void*,const void*,unsigned);
        ArmCopy fn=(ArmCopy)(uintptr_t)(base32+0x016b4da0u);
        unsigned char source[128]={},dest[128]={};
        for(unsigned j=0;j<sizeof(source);j++)source[j]=(unsigned char)(j^0x7bu);
        log_line("COBALT_MEMCPY_ENTER");
        fn(dest,source,sizeof(source));
        log_line("COBALT_MEMCPY_RETURN");
        check(memcmp(dest,source,sizeof(source))==0,
              "official Cobalt internal __aeabi_memcpy8 copies 128 bytes");
        if(failures)goto release;
      }
'''
s=s.replace(anchor,new,1)
p.write_text(s,encoding='utf-8')
print('ARM_INTERNAL_PLT_MEMCPY_EXECUTION_INSTRUMENTED')

