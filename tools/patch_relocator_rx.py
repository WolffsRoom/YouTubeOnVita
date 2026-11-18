from pathlib import Path
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita\relocate_elf.c')
s=p.read_text(encoding='utf-8-sig')
s=s.replace('#include <psp2/kernel/processmgr.h>','#include <psp2/kernel/processmgr.h>\n#include <kubridge.h>')
needle='''      check(image[0]==0x7f && image[1]=='E' && image[2]=='L' && image[3]=='F',
            "in-memory ELF header intact after relocations");'''
assert needle in s
add='''      // Read-only-executable protection gate: we never invoke the Cobalt code.
      // Dynamic relocations in this official image all target RW data segments,
      // so protecting its RX segment does not interfere with unresolved imports.
      uint32_t xstart=0,xlen=0;unsigned execs=0;
      for(unsigned i=0;i<nheaders;i++){
        const Elf32_Phdr *p=&headers[i];
        if(p->p_type==PT_LOAD && (p->p_flags & PF_X)){
          execs++;
          uint32_t lo=p->p_vaddr & ~4095u;
          uint32_t hi=(uint32_t)(((uint64_t)p->p_vaddr+p->p_memsz+4095u)&~4095ull);
          xstart=lo;xlen=hi-lo;
        }
      }
      check(execs==1 && xstart==0x818000u &&
            xlen==(0x1fa0000u-0x818000u) &&
            (uint64_t)xstart+xlen<=IMAGE_CAPACITY,
            "precise alignment and size of official Cobalt ARM executable segment");
      if(failures)goto release;
      uint8_t first_text_bytes[16];
      memcpy(first_text_bytes,image+xstart,16);
      int protection=kuKernelMemProtect(image+xstart,xlen,
                                        KU_KERNEL_PROT_READ|KU_KERNEL_PROT_EXEC);
      note_u32("COBALT_TEXT_RX_PROTECTION", (unsigned)protection, xlen);
      check(protection==0,"kubridge maps official Cobalt code segment R-X");
      if(failures)goto release;
      kuKernelFlushCaches(image+xstart,xlen);
      check(memcmp(first_text_bytes,image+xstart,16)==0,
            "Cobalt original text remains intact after RX permissions");

      // A trusted, 2-instruction test function goes in the unused gap before
      // the official text PT_LOAD. It is not part of any Cobalt segment.
      const uint32_t stub_offset=xstart-0x1000u;
      check(stub_offset>=0x808e10u && !in_load(stub_offset,8),
            "trusted executable test page is outside every Cobalt PT_LOAD segment");
      if(failures)goto release;
      const uint32_t demo[2]={0xe3a0002au,0xe12fff1eu}; // mov r0,#42; bx lr
      memcpy(image+stub_offset,demo,sizeof(demo));
      protection=kuKernelMemProtect(image+stub_offset,0x1000,
                                     KU_KERNEL_PROT_READ|KU_KERNEL_PROT_EXEC);
      check(protection==0,"trusted test page separately mapped R-X inside 34MB image");
      if(failures)goto release;
      kuKernelFlushCaches(image+stub_offset,sizeof(demo));
      int (*known_function)(void)=(int (*)(void))(image+stub_offset);
      int trusted_return=known_function();
      note_u32("TRUSTED_ARM_EXEC_RETURN", (unsigned)trusted_return,stub_offset);
      check(trusted_return==42,"trusted ARM code executes inside full Cobalt allocation");

      // PT_LOAD #4 has a zero-initialized BSS tail. Data must stay writable.
      uint32_t bss_offset=0x203d9f0u;
      check(in_load(bss_offset,4),"BSS address belongs to writable data PT_LOAD");
      if(failures)goto release;
      uint32_t *bss=(uint32_t*)(image+bss_offset);
      uint32_t saved=*bss;
      *bss=0x53425654u;
      check(*bss==0x53425654u,"writable Cobalt BSS survives separate RX mapping");
      *bss=saved;
      log_line("COBALT_CODE_EXECUTION_NOT_ATTEMPTED");
'''
s=s.replace(needle,needle+'\n'+add)
p.write_text(s,encoding='utf-8')
cm=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\CMakeLists.txt')
c=cm.read_text(encoding='utf-8-sig')
needle='target_link_libraries(ytv_relocator PRIVATE SceIofilemgr_stub SceLibKernel_stub SceSysmem_stub)'
assert needle in c
c=c.replace(needle,'target_link_libraries(ytv_relocator PRIVATE kubridge_stub SceIofilemgr_stub SceLibKernel_stub SceSysmem_stub)')
cm.write_text(c,encoding='utf-8')
print('RX_EXECUTABLE_SEGMENT_VALIDATION_ADDED')

