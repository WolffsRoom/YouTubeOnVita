from pathlib import Path
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita\evergreen_inspect.c')
s=p.read_text()
a='    log_msg(message);\n    check(read_ok && pos==length,"complete 33 MB Cobalt ELF read in Vita3K");'
b='    log_msg(message);\n    stream_hash=checksum;\n    check(read_ok && pos==length,"complete 33 MB Cobalt ELF read in Vita3K");'
assert a in s
s=s.replace(a,b)
a='''      check(read_ok&&copied==length &&
            memcmp(base,ELFMAG,SELFMAG)==0 &&
            ((uint8_t*)base)[length-1]!=0xFF,
            "complete official ELF copied to contiguous Vita memory");'''
b='''      uint32_t copy_hash=0;
      for(int64_t i=0;i<copied;i++)
        copy_hash=(copy_hash*33u)^((const uint8_t*)base)[i];
      snprintf(message,sizeof(message),"RAM_COPY_HASH32 %08X",copy_hash);
      log_msg(message);
      check(read_ok&&copied==length &&
            memcmp(base,ELFMAG,SELFMAG)==0 &&
            stream_hash==copy_hash,
            "complete official ELF copied to contiguous Vita memory");'''
assert a in s
s=s.replace(a,b)
p.write_text(s,encoding='utf-8')
print('CHECKSUM_COMPARISON_CORRECTED')

