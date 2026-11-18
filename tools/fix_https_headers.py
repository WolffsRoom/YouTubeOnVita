from pathlib import Path
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita\https_probe.cpp')
s=p.read_text(encoding='utf-8')
needle='''      if(tmpl>=0){
        sceHttpSetAutoRedirect(tmpl,1);'''
replacement='''      if(tmpl>=0){
        int maxheader=sceHttpSetResponseHeaderMaxSize(tmpl,65536);
        log_result("HEADER_LIMIT_TEMPLATE_64K",maxheader);
        sceHttpSetAutoRedirect(tmpl,1);'''
assert needle in s
s=s.replace(needle,replacement)
needle='''          if(req>=0){
            int sent=sceHttpSendRequest(req,nullptr,0);'''
repl='''          if(req>=0){
            int header_limit=sceHttpSetResponseHeaderMaxSize(req,65536);
            log_result("HEADER_LIMIT_REQUEST_64K",header_limit);
            int sent=sceHttpSendRequest(req,nullptr,0);'''
assert needle in s
s=s.replace(needle,repl)
p.write_text(s,encoding='utf-8')
print('SCEHTTP_64KB_HEADER_LIMIT_APPLIED_WITH_TLS_VERIFICATION_PRESERVED')

