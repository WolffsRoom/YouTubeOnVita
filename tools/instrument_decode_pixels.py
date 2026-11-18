from pathlib import Path
p=Path(r'C:\Users\wolff\Documents\SDKVita\YouTubeVita\experiments\cobalt\vita\decode_frame_probe.c')
s=p.read_text(encoding='utf-8-sig')
needle='''                  check(decoded>=0 && pictures.numOfOutput==1,
                        "native H264 decoder produced one video frame");'''
replace='''                  status("PIC_OUTPUT_BUFFER_BEFORE",(int)(uintptr_t)output);
                  status("PIC_OUTPUT_BUFFER_AFTER",(int)(uintptr_t)pic.frame.pPicture[0]);
                  status("ARRAY_PICTURE_PTR",(int)(uintptr_t)pictures.pPicture[0]);
                  status("PIC_PIXEL_FORMAT",(int)pic.frame.pixelType);
                  status("PIC_PITCH",(int)pic.frame.framePitch);
                  status("PIC_POINTER_1",(int)(uintptr_t)pic.frame.pPicture[1]);
                  status("PICTURE_SIZE_FIELD",(int)pic.size);
                  check(decoded>=0 && pictures.numOfOutput==1,
                        "native H264 decoder reports one output frame");'''
assert needle in s
p.write_text(s.replace(needle,replace),encoding='utf-8')
print('DECODER_OUTPUT_POINTER_DIAGNOSTICS_ADDED')

