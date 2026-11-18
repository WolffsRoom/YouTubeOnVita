import ctypes,io,base64
from ctypes import wintypes
from PIL import ImageGrab
u=ctypes.windll.user32
hwnd=u.GetForegroundWindow()
r=wintypes.RECT();u.GetWindowRect(hwnd,ctypes.byref(r))
name=ctypes.create_unicode_buffer(255);u.GetWindowTextW(hwnd,name,255)
print('FG',name.value,'RECT',r.left,r.top,r.right,r.bottom)
im=ImageGrab.grab(bbox=(r.left,r.top,r.right,r.bottom))
im.thumbnail((880,550))
o=io.BytesIO();im.save(o,'PNG',optimize=True)
print('B64START')
print(base64.b64encode(o.getvalue()).decode())
