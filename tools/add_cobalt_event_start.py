from pathlib import Path
r=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
src=r/'experiments/cobalt/vita/relocate_elf.c'
s=src.read_text(encoding='utf-8-sig')
anchor='''      log_line("COBALT_CONSTRUCTORS_COMPLETE");'''
assert anchor in s
s=s.replace(anchor,anchor+r'''
#if defined(YTV_COBALT_BOOT_STAGE) && YTV_COBALT_BOOT_STAGE>=2
      // The official Cobalt ELF exports SbEventHandle at fixed offset
      // 0x00818f7c (confirmed in readelf). A standalone test wires it into
      // our real Vita lifecycle dispatcher, with no mock/simulated UI.
      extern _Bool ytv_vita_lifecycle_begin(void(*)(const void*));
      extern int ytv_starboard_pump_scheduled(int);
      typedef void (*CobaltEventHandler)(const void*);
      const uint32_t cobalt_event_offset=0x00818f7cu;
      check(cobalt_event_offset>=xstart &&
            cobalt_event_offset<xstart+xlen,
            "verified official Cobalt SbEventHandle inside executable segment");
      if(failures)goto release;
      CobaltEventHandler cobalt_event=(CobaltEventHandler)(
                               uintptr_t)(base32+cobalt_event_offset);
      log_line("COBALT_EVENT_START_ENTER");
      int started=ytv_vita_lifecycle_begin(cobalt_event);
      log_line("COBALT_EVENT_START_RETURN");
      check(started,"official Cobalt SbEventHandle received native Starboard Start");
      if(failures)goto release;
      unsigned dispatched=0;
      for(unsigned j=0;j<180;j++){
        int n=ytv_starboard_pump_scheduled(24);
        if(n>0)dispatched+=(unsigned)n;
        if(j==0 || j==29 || j==89 || j==179)
          note_u32("COBALT_EVENT_PUMP",j,dispatched);
        sceKernelDelayThread(16667);
      }
      log_line("COBALT_EVENT_PUMP_COMPLETE");
#endif
''')
src.write_text(s,encoding='utf-8')
cm=r/'CMakeLists.txt';s=cm.read_text(encoding='utf-8-sig')
old='target_compile_definitions(ytv_cobalt_boot PRIVATE YTV_COBALT_BOOT_STAGE=1)'
assert old in s
cm.write_text(s.replace(old,'target_compile_definitions(ytv_cobalt_boot PRIVATE YTV_COBALT_BOOT_STAGE=2)'),encoding='utf-8')
print('COBALT_OFFICIAL_SB_EVENT_START_PROBE_ADDED')

