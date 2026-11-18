from pathlib import Path
root=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita')
p=root/'experiments/cobalt/vita/relocate_elf.c'
s=p.read_text(encoding='utf-8')
needle='#define RELOC_R_ARM_JUMP_SLOT 22u'
assert needle in s
s=s.replace(needle,needle+'''
extern uintptr_t ytv_find_real_starboard_export(const char *name);
extern unsigned ytv_real_starboard_export_count(void);
''')
needle='int main(void) {'
assert needle in s
extra=r'''static int apply_real_imports(uint32_t symtab,uint32_t symcount,
                              uint32_t rela,uint32_t relsz,
                              uint32_t jmpa,uint32_t jmpsz,
                              unsigned *linked,unsigned *unresolved) {
  uint32_t strtab=0,strsz=0;
  for(unsigned i=0;i<nheaders;i++) {
    const Elf32_Phdr *p=&headers[i];
    if(p->p_type!=PT_DYNAMIC)continue;
    const Elf32_Dyn *d=(const Elf32_Dyn*)(image+p->p_vaddr);
    for(unsigned j=0;j<p->p_memsz/sizeof(Elf32_Dyn);j++) {
      if(d[j].d_tag==DT_STRTAB)strtab=d[j].d_un.d_ptr;
      if(d[j].d_tag==DT_STRSZ)strsz=d[j].d_un.d_val;
      if(d[j].d_tag==DT_NULL)break;
    }
  }
  if(strtab==0 || strsz==0 || !in_load(strtab,strsz) ||
     !in_load(symtab,symcount*sizeof(Elf32_Sym)))return 0;
  const uint32_t start[2]={rela,jmpa};
  const uint32_t size[2]={relsz,jmpsz};
  *linked=0;*unresolved=0;
  for(unsigned t=0;t<2;t++) {
    const Elf32_Rel *rels=(const Elf32_Rel*)(image+start[t]);
    for(unsigned i=0;i<size[t]/sizeof(Elf32_Rel);i++) {
      uint32_t kind=ELF32_R_TYPE(rels[i].r_info);
      if(kind!=RELOC_R_ARM_GLOB_DAT && kind!=RELOC_R_ARM_JUMP_SLOT)continue;
      unsigned idx=ELF32_R_SYM(rels[i].r_info);
      if(idx>=symcount || !in_load(rels[i].r_offset,4))return 0;
      const Elf32_Sym *sym=(const Elf32_Sym*)(image+symtab+idx*sizeof(Elf32_Sym));
      if(sym->st_shndx!=SHN_UNDEF)continue;
      if(sym->st_name>=strsz)return 0;
      const char *name=(const char*)(image+strtab+sym->st_name);
      unsigned available=strsz-sym->st_name;
      if(!memchr(name,0,available))return 0;
      uintptr_t address=ytv_find_real_starboard_export(name);
      if(address!=0) {
        *(uint32_t*)(image+rels[i].r_offset)=(uint32_t)address;
        if(*(uint32_t*)(image+rels[i].r_offset)!=(uint32_t)address)return 0;
        (*linked)++;
      } else {
        // Leave missing imports untouched; do NOT replace them with a fake stub.
        (*unresolved)++;
      }
    }
  }
  return 1;
}
'''
s=s.replace(needle,extra+'\n'+needle)
needle='''      check(image[0]==0x7f && image[1]=='E' && image[2]=='L' && image[3]=='F',
            "in-memory ELF header intact after relocations");'''
assert needle in s
add='''      unsigned imported=0,still_missing=0;
      int imports_valid=apply_real_imports(sa,symcount,ra,rl,ja,jl,
                                           &imported,&still_missing);
      note_u32("REAL_STARBOARD_EXPORTS_LINKED",imported,still_missing);
      check(imports_valid && imported==12 && still_missing==199 &&
            ytv_real_starboard_export_count()==12,
            "12 real Vita Starboard exports linked; 199 missing imports left unresolved");
      if(failures)goto release;'''
s=s.replace(needle,needle+'\n'+add)
p.write_text(s,encoding='utf-8')
cm=root/'CMakeLists.txt'
t=cm.read_text(encoding='utf-8')
assert 'add_library(starboard_vita15 STATIC' in t
t=t.replace('"\u0024{VITA}/pad_adapter.cpp")','"\u0024{VITA}/pad_adapter.cpp" "\u0024{VITA}/import_lookup.cpp")')
t=t.replace('target_link_libraries(ytv_relocator PRIVATE kubridge_stub SceIofilemgr_stub SceLibKernel_stub SceSysmem_stub)',
'''target_link_libraries(ytv_relocator PRIVATE starboard_vita15 kubridge_stub
     SceKernelThreadMgr_stub SceIofilemgr_stub SceLibKernel_stub SceSysmem_stub)''')
cm.write_text(t,encoding='utf-8')
print('IMPORT_RESOLVER_INSTALLED')

