#!/usr/bin/env python3
import os,sys,termios,time,select,re
PORT=sys.argv[1] if len(sys.argv)>1 else "/dev/cu.usbserial-A5069RR4"
NAME={0:"PA0",1:"PA1",2:"PA2",3:"PA3",4:"PA4",5:"PA7",6:"PA8",11:"PA13",12:"PA14",15:"PA17",16:"PA18",17:"PA19",18:"PA20"}
PAIRS=[(5,6),(5,2),(6,2),(5,3),(6,3),(5,4),(6,4)]
fd=os.open(PORT,os.O_RDWR|os.O_NOCTTY|os.O_NONBLOCK)
a=termios.tcgetattr(fd);B=termios.B115200
a[0]=0;a[1]=0;a[2]=termios.CS8|termios.CREAD|termios.CLOCAL;a[3]=0;a[4]=B;a[5]=B
termios.tcsetattr(fd,termios.TCSANOW,a);termios.tcflush(fd,termios.TCIOFLUSH)
def send(s):os.write(fd,s.encode()+b"\r\n");time.sleep(0.08)
def read_for(t):
    buf=b"";e=time.time()+t
    while time.time()<e:
        if select.select([fd],[],[],0.08)[0]:
            try:buf+=os.read(fd,4096)
            except BlockingIOError:pass
    return buf.decode("latin1","replace")
def snap():
    termios.tcflush(fd,termios.TCIFLUSH);send("s");txt=read_for(1.0);line=""
    for l in txt.splitlines():
        if "[snap]" in l:line=l
    return {m.group(1):int(m.group(2)) for m in re.finditer(r"(PA\d+)=(-?\d+)",line)}
def setlvl(d,want):
    for _ in range(3):
        if snap().get(NAME[d])==want:return True
        send("t%d"%d);time.sleep(0.12)
    return snap().get(NAME[d])==want
t0=time.time()
def ts():return "%5.1fs"%(time.time()-t0)
print("START",flush=True)
for a_d,b_d in PAIRS:
    an,bn=NAME[a_d],NAME[b_d]
    send("r");time.sleep(0.3)
    setlvl(b_d,0);setlvl(a_d,1)
    print(f"[{ts()}] {an}+{bn}  SET  ({an}=H {bn}=L)  -- listen 3s",flush=True);time.sleep(3)
    setlvl(a_d,0);setlvl(b_d,1)
    print(f"[{ts()}] {an}+{bn}  RESET ({an}=L {bn}=H)  -- listen 3s",flush=True);time.sleep(3)
send("r");os.close(fd)
print("DONE",flush=True)
