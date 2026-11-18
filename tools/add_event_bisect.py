from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=r/'experiments/cobalt/vita/relocate_elf.c'
s=p.read_text(encoding='utf-8-sig')
anchor='''      uint8_t first_text_bytes[16];'''
assert anchor in s
patch=r'''
#if defined(YTV_COBALT_DIAG_EVENT_CUTOFF)
      {
        unsigned target=0;
        int f=sceIoOpen("ux0:data/youtubevita/event_cutoff.txt",
                       SCE_O_RDONLY,0);
        if(f>=0){
          char value[24]={0};
          int n=sceIoRead(f,value,sizeof(value)-1);
          sceIoClose(f);
          if(n>0 && sscanf(value,"%x",&target)!=1)target=0;
        }
        if(target){
          // Valid instruction boundaries inside official Start event handler.
          const unsigned allowed[]={
            0x819160u,0x81916cu,0x81917cu,0x819180u,
            0x819188u,0x819194u,0x81919cu,0x8191a8u,
            0x8191b4u,0x8191c4u,0x8191c8u,0x8191d0u,
            0x8191e0u,0x819200u,0x819204u,0x819210u
          };
          int okay=0;
          for(unsigned i=0;i<sizeof(allowed)/sizeof(allowed[0]);i++)
            if(target==allowed[i])okay=1;
          check(okay && in_load(target,8),"validated isolated Start event cutoff");
          if(failures)goto release;
          uint32_t* instr=(uint32_t*)(image+target);
          instr[0]=0xe28dd0b0u;  // add sp,sp,#176
          instr[1]=0xe8bd88f0u;  // pop {r4,r5,r6,r7,fp,pc}
          note_u32("COBALT_EVENT_CUTOFF",target,1);
        }
      }
#endif
'''
s=s.replace(anchor,patch+anchor,1)
p.write_text(s,encoding='utf-8')
cm=r/'CMakeLists.txt'
s=cm.read_text(encoding='utf-8-sig')
old='target_compile_definitions(ytv_cobalt_boot PRIVATE YTV_COBALT_BOOT_STAGE=2)'
assert old in s
s=s.replace(old,'target_compile_definitions(ytv_cobalt_boot PRIVATE YTV_COBALT_BOOT_STAGE=2 YTV_COBALT_DIAG_EVENT_CUTOFF=1)')
cm.write_text(s,encoding='utf-8')
print('EVENT_START_CUTOFF_DIAGNOSTICS_ADDED')

