import struct,sys
def ev(t,c,v): return struct.pack('<IIHHi',0,0,t,c,v)
x,y=int(sys.argv[1]),int(sys.argv[2])
down=ev(3,0x2f,0)+ev(3,0x39,77)+ev(3,0x35,x)+ev(3,0x36,y)+ev(1,0x14a,1)+ev(0,0,0)
up=ev(3,0x2f,0)+ev(3,0x39,-1)+ev(1,0x14a,0)+ev(0,0,0)
open('down.bin','wb').write(down); open('up.bin','wb').write(up)
