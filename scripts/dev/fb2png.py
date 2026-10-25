import sys
d=open(sys.argv[1],'rb').read()
w,h,ll=758,1024,768
out=bytearray(b'P5 %d %d 255\n'%(w,h))
for y in range(h): out+=d[y*ll:y*ll+w]
open(sys.argv[2],'wb').write(out)
