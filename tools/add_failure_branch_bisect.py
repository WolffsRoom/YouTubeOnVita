from pathlib import Path
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita\relocate_elf.c')
s=p.read_text(encoding='utf-8-sig')
old='''            0x8191e0u,0x819200u,0x819204u,0x819210u'''
new='''            0x8191e0u,0x819200u,0x819204u,0x819210u,
            0x8193f8u,0x8193fcu,0x819400u,0x819440u,
            0x819444u,0x819454u,0x819458u,0x819464u,
            0x819468u,0x819474u'''
assert old in s
p.write_text(s.replace(old,new),encoding='utf-8')
print('BRANCH_FAILURE_START_DIAGNOSTIC_CUTOFFS_ADDED')

