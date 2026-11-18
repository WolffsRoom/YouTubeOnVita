from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=r/'experiments/cobalt/vita/relocate_elf.c';s=p.read_text(encoding='utf-8-sig')
anchor='''      uint8_t first_text_bytes[16];''';assert anchor in s
patch=r'''
#if defined(YTV_COBALT_DIAG_EVENT_CUTOFF)
      // Instrument only the private RW image before mapping RX: test Cobalt
      // start's nested configuration function in isolation.
      {
        unsigned target=0;
        int f=sceIoOpen("ux0:data/youtubevita/inner_cutoff.txt",
                        SCE_O_RDONLY,0);
        if(f>=0){
          char value[24]={0};
          int n=sceIoRead(f,value,sizeof(value)-1);
          sceIoClose(f);
          if(n>0 && sscanf(value,"%x",&target)!=1)target=0;
        }
        if(target){
          const unsigned allowed[]={
            0x13210a8u,0x13210acu,0x13210c0u,0x13210ccu,
            0x13210d0u,0x13210e0u,0x13210e4u,0x1321160u,
            0x1321164u,0x1321170u,0x13211b8u,0x13211ccu,
            0x13211dcu,0x13211ecu,0x13211f4u,0x132124cu
          };
          int okay=0;
          for(unsigned i=0;i<sizeof(allowed)/sizeof(allowed[0]);i++)
            if(target==allowed[i])okay=1;
          check(okay && in_load(target,8),"verified nested Cobalt Start cutoff");
          if(failures)goto release;
          uint32_t* instr=(uint32_t*)(image+target);
          instr[0]=0xe28dd0c0u; // add sp,sp,#192
          instr[1]=0xe8bd8070u; // pop {r4,r5,r6,pc}
          note_u32("COBALT_INNER_CUTOFF",target,1);
        }
      }
#endif
'''
s=s.replace(anchor,patch+anchor,1);p.write_text(s,encoding='utf-8')
print('NESTED_COBALT_START_CUTOFF_DIAGNOSTICS_ADDED')

