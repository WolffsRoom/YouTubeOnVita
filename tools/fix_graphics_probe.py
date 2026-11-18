from pathlib import Path
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita\graphics_test.cpp')
s=p.read_text(encoding='utf-8-sig')
old=''' GLboolean success=vglInit(0x80000);
 check(success==GL_TRUE,"vitaGL GXM context initialized successfully");
 if(success!=GL_TRUE)goto done;'''
new=''' // vitaGL returns GL_FALSE for native resolution and GL_TRUE only if
 // it has silently reduced the requested framebuffer size. Neither value
 // indicates initializer failure! Validate EGL and rendered frames instead.
 GLboolean fallback=vglInitExtended(0x80000,960,544,0x2000000,
                                    SCE_GXM_MULTISAMPLE_NONE);
 check(fallback==GL_FALSE,
       "vitaGL GXM accepted native 960x544 without resolution fallback");'''
assert old in s
s=s.replace(old,new)
p.write_text(s,encoding='utf-8')
print('CORRECTED_VITAGL_INIT_RETURN_SEMANTICS_AND_DISABLED_MSAA')

