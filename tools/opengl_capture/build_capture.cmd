@echo off
set D=%~dp0
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
cl /nologo /LD /O2 /EHsc /MD /utf-8 /std:c++20 /I"%D%MinHook\include" /Fo"%D%\" /Fe:"%D%capture_swap.dll" "%D%capture_swap.cpp" "%D%MinHook\src\buffer.c" "%D%MinHook\src\hook.c" "%D%MinHook\src\trampoline.c" "%D%MinHook\src\hde\hde64.c" /link opengl32.lib gdi32.lib user32.lib
