from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=r/'experiments/cobalt/vita/relocate_elf.c'
s=p.read_text(encoding='utf-8-sig')
anchor='''      uint8_t first_text_bytes[16];'''
assert anchor in s
patch=r'''
#if defined(YTV_COBALT_DIAG_EVENT_CUTOFF)
      // Only the isolated diagnostic title can bisect the real SbOnce init
      // callback. Does not touch the official ELF file or normal relocation.
      {
        unsigned target=0;
        int f=sceIoOpen("ux0:data/youtubevita/init_cutoff.txt",SCE_O_RDONLY,0);
        if(f>=0){
          char value[24]={0};
          int n=sceIoRead(f,value,sizeof(value)-1);
          sceIoClose(f);
          if(n>0 && sscanf(value,"%x",&target)!=1)target=0;
        }
        if(target){
          const unsigned allowed[]={
            0x15dc4dcu,0x15dc4f0u,0x15dc524u,0x15dc534u,
            0x15dc538u,0x15dc540u,0x15dc544u,0x15dc54cu,
            0x15dc59cu,0x15dc5b8u,0x15dc5c8u,0x15dc5dcu,
            0x15dc5ecu,0x15dc604u,0x15dc614u
          };
          int okay=0;
          for(unsigned i=0;i<sizeof(allowed)/sizeof(allowed[0]);i++)
            if(allowed[i]==target)okay=1;
          check(okay && in_load(target,8),"validated isolated Cobalt Once cutoff");
          if(failures)goto release;
          uint32_t* ins=(uint32_t*)(image+target);
          ins[0]=0xe28dd020u; // add sp,sp,#32
          ins[1]=0xe8bd81f0u; // pop {r4,r5,r6,r7,r8,pc}
          note_u32("COBALT_ONCE_INIT_CUTOFF",target,1);
        }
      }
#endif
'''
s=s.replace(anchor,patch+anchor,1)
p.write_text(s,encoding='utf-8')
print('COBALT_ONCE_INIT_DIAGNOSTIC_GATE_READY')

