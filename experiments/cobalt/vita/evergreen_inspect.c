// Vita3K read-only validation of Google-provided Cobalt Evergreen 5.50.2.
// This file INSPECTS an ELF32 image. It does not load or execute the image.
#include <elf.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

unsigned int sceUserMainThreadStackSize=1024*1024;
unsigned int _newlib_heap_size_user=8*1024*1024;

static int passed=0, failed=0;
static void log_msg(const char *msg) {
  sceIoMkdir("ux0:data/youtubevita",0777);
  int fd=sceIoOpen("ux0:data/youtubevita/evergreen_inspect.log",
     SCE_O_CREAT | SCE_O_WRONLY | SCE_O_APPEND,0666);
  if(fd>=0){sceIoWrite(fd,msg,strlen(msg));sceIoWrite(fd,"\n",1);sceIoClose(fd);}
}
static void check(int condition,const char *name){
  char line[180];
  snprintf(line,sizeof(line),"%s %s",condition?"PASS":"FAIL",name);
  log_msg(line);
  if(condition)passed++;else failed++;
}
static int read_at(SceUID file,int64_t offset,void *dst,unsigned len){
  if(sceIoLseek(file,offset,SCE_SEEK_SET)<0)return 0;
  return sceIoRead(file,dst,len)==(int)len;
}

int main(void){
  log_msg("\nSTART Cobalt 5.50.2 official ELF32 read-only Vita test");
  SceUID file=sceIoOpen("app0:libcobalt.so",SCE_O_RDONLY,0);
  check(file>=0,"official libcobalt.so available in Vita filesystem");
  if(file<0){sceKernelExitProcess(1);return 1;}
  int64_t length=sceIoLseek(file,0,SCE_SEEK_END);
  char message[140];
  snprintf(message,sizeof(message),"FILE_BYTES %lld",(long long)length);
  log_msg(message);
  check(length==33491716LL,"exact Google Cobalt 5.50.2 ELF byte count");

  Elf32_Ehdr eh={};
  check(read_at(file,0,&eh,sizeof(eh)),"read ELF32 header");
  check(memcmp(eh.e_ident,ELFMAG,SELFMAG)==0,
        "genuine ELF magic");
  check(eh.e_ident[EI_CLASS]==ELFCLASS32 &&
        eh.e_ident[EI_DATA]==ELFDATA2LSB,
        "ELF32 little endian");
  check(eh.e_machine==EM_ARM && eh.e_type==ET_DYN,
        "ARM shared library ET_DYN");
  check((eh.e_flags&0x00000400u)!=0,
        "ARM EABI hardfloat shared object");
  check(eh.e_phentsize==sizeof(Elf32_Phdr) &&
        eh.e_phnum>0 && eh.e_phnum<=32,"program header table valid");
  int loads=0, dynamic=0;uint64_t max_end=0; int ranges=1;
  if(eh.e_phnum>0&&eh.e_phnum<=32&&eh.e_phentsize==sizeof(Elf32_Phdr)){
    for(int i=0;i<eh.e_phnum;i++){
      Elf32_Phdr ph={};
      if(!read_at(file,(int64_t)eh.e_phoff+i*sizeof(ph),&ph,sizeof(ph))){
        ranges=0;break;
      }
      if((uint64_t)ph.p_offset+ph.p_filesz>(uint64_t)length)ranges=0;
      if(ph.p_type==PT_LOAD){
        loads++;
        uint64_t end=(uint64_t)ph.p_vaddr+ph.p_memsz;
        if(end>max_end)max_end=end;
      }
      if(ph.p_type==PT_DYNAMIC)dynamic++;
    }
  } else ranges=0;
  check(ranges,"all program headers fall within ELF file");
  check(loads==4,"four PT_LOAD segments detected");
  check(dynamic==1,"one PT_DYNAMIC section detected");
  snprintf(message,sizeof(message),"ELF_VIRTUAL_END %llu",(unsigned long long)max_end);
  log_msg(message);
  check(max_end>30000000u && max_end<50000000u,
        "virtual layout is consistent with 33 MB library");
  // Stream through image using 64 KB buffer, without mapping executable memory.
  uint32_t stream_hash=0;
  uint8_t *buffer=(uint8_t*)malloc(65536);
  check(buffer!=NULL,"64KB streaming allocation");
  if(buffer){
    int64_t pos=0;uint32_t checksum=0; int read_ok=1;
    while(pos<length){
      unsigned amount=(unsigned)((length-pos)>65536?65536:(length-pos));
      if(!read_at(file,pos,buffer,amount)){read_ok=0;break;}
      for(unsigned i=0;i<amount;i++)checksum=(checksum*33u)^buffer[i];
      pos+=amount;
    }
    snprintf(message,sizeof(message),"STREAMED_BYTES %lld HASH32 %08X",(long long)pos,checksum);
    log_msg(message);
    stream_hash=checksum;
    check(read_ok && pos==length,"complete 33 MB Cobalt ELF read in Vita3K");
    free(buffer);
  }
  // Loader-memory feasibility only. Never make this block executable.
  // Vita memory blocks must be page aligned; 34 MiB comfortably contains ELF image.
  const unsigned int capacity = 34u*1024u*1024u;
  SceUID block = sceKernelAllocMemBlock("CobaltELFData",SCE_KERNEL_MEMBLOCK_TYPE_USER_RW,
                                         capacity,NULL);
  check(block>=0,"Vita can allocate one 34MB contiguous user-memory block");
  if(block>=0){
    void *base=NULL;
    int got=sceKernelGetMemBlockBase(block,&base);
    check(got==0&&base!=NULL,"Vita memory block mapped to user space");
    if(got==0&&base){
      int64_t copied=0;
      int read_ok=1;
      while(copied<length){
        unsigned int amount=(unsigned int)((length-copied)>65536?65536:(length-copied));
        if(!read_at(file,copied,(uint8_t*)base+copied,amount)){
          read_ok=0;break;
        }
        copied+=amount;
      }
      uint32_t copy_hash=0;
      for(int64_t i=0;i<copied;i++)
        copy_hash=(copy_hash*33u)^((const uint8_t*)base)[i];
      snprintf(message,sizeof(message),"RAM_COPY_HASH32 %08X",copy_hash);
      log_msg(message);
      check(read_ok&&copied==length &&
            memcmp(base,ELFMAG,SELFMAG)==0 &&
            stream_hash==copy_hash,
            "complete official ELF copied to contiguous Vita memory");
    }
    check(sceKernelFreeMemBlock(block)==0,"34MB Vita memory block released");
  }
  sceIoClose(file);
  snprintf(message,sizeof(message),"RESULT %s (%d passed, %d failed)",
           failed?"FAIL":"PASS",passed,failed);
  log_msg(message);
  sceKernelExitProcess(failed?1:0);
  return failed;
}

