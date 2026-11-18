from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=r/'experiments/cobalt/vita/relocate_elf.c'
s=p.read_text(encoding='utf-8-sig')
anchor='''      uint8_t first_text_bytes[16];'''
assert anchor in s
snippet=r'''
#if defined(YTV_COBALT_DIAG_CTOR_CUTOFF)
      // Debug VPK modifies only its private RAM copy of one constructor,
      // before mapping RX. Epilogue matches push-r4-lr/sub-sp-128 prologue.
      {
        unsigned target=0;
        int diag_fd=sceIoOpen("ux0:data/youtubevita/ctor_cutoff.txt",SCE_O_RDONLY,0);
        if(diag_fd>=0){
          char value[24]={0};
          int n=sceIoRead(diag_fd,value,sizeof(value)-1);
          sceIoClose(diag_fd);
          if(n>0){
            unsigned scan=0;
            if(sscanf(value,"%x",&scan)==1)target=scan;
          }
        }
        if(target!=0){
          int allowed=target==0x819c74u || target==0x819c8cu ||
              target==0x819c98u || target==0x819cb0u || target==0x819cc8u;
          check(allowed && in_load(target,8),
               "constructor debug cutoff selected only from verified epilogues");
          if(failures)goto release;
          uint32_t* instr=(uint32_t*)(image+target);
          instr[0]=0xe28dd080u; // add sp,sp,#128
          instr[1]=0xe8bd8010u; // pop {r4,pc}
          note_u32("COBALT_CTOR_DEBUG_CUTOFF",target,1);
        }
      }
#endif
'''
s=s.replace(anchor,snippet+anchor,1)
needle='''        log_line(item);
      }
      log_line("COBALT_CONSTRUCTORS_COMPLETE");'''
assert needle in s
repl='''        log_line(item);
#if defined(YTV_COBALT_DIAG_CTOR_CUTOFF)
        log_line("COBALT_DEBUG_ONE_CTOR_FINISHED");
        break; // diagnostic only
#endif
      }
      log_line("COBALT_CONSTRUCTORS_COMPLETE");'''
s=s.replace(needle,repl,1)
p.write_text(s,encoding='utf-8')
cm=r/'CMakeLists.txt'
s=cm.read_text(encoding='utf-8-sig')
a='target_compile_definitions(ytv_cobalt_boot PRIVATE YTV_COBALT_BOOT_STAGE=1)'
b='target_compile_definitions(ytv_cobalt_boot PRIVATE YTV_COBALT_BOOT_STAGE=1 YTV_COBALT_DIAG_CTOR_CUTOFF=1)'
assert a in s
cm.write_text(s.replace(a,b),encoding='utf-8')
print('SAFE_DEVELOPMENT_MEMORY_PATCH_GATE_READY')

