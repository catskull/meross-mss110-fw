#!/usr/bin/env python3
"""
Latching-relay combination tester for the MSS110 (scanner+OTA firmware).

A bistable/latching relay is driven as an H-bridge: two pins held at OPPOSITE
levels for a moment sets the relay one way (click ON); swapping them sets it the
other way (click OFF). Driving a single pin does nothing - which is what we saw.

This drives candidate PAIRS to opposite levels and swaps them, pausing so you can
listen for the click. It talks to the firmware's 't<Dnum>' (toggle) and 's'
(snapshot) commands, reading the snapshot back to force each pin to a known level.

Run:  python3 combo_test.py [/dev/cu.usbserial-A5069RR4]
"""
import os, sys, termios, time, select, re

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/cu.usbserial-A5069RR4"

# Drivable pins (Dnum -> name). Excludes flash pins PA9-PA12 (D7-D10) and
# serial pins PA15/PA16, which must never be reconfigured.
NAME = {0:"PA0",1:"PA1",2:"PA2",3:"PA3",4:"PA4",5:"PA7",6:"PA8",
        11:"PA13",12:"PA14",15:"PA17",16:"PA18",17:"PA19",18:"PA20"}

# Pairs to try, most-likely first. PA7+PA8 are the two free flash-range pins.
PAIRS = [(5,6),(5,2),(6,2),(5,3),(6,3),(5,4),(6,4),(2,3),(3,4),(2,4)]

fd = os.open(PORT, os.O_RDWR | os.O_NOCTTY | os.O_NONBLOCK)
a = termios.tcgetattr(fd); B = termios.B115200
a[0]=0; a[1]=0; a[2]=termios.CS8|termios.CREAD|termios.CLOCAL; a[3]=0; a[4]=B; a[5]=B
termios.tcsetattr(fd, termios.TCSANOW, a)
termios.tcflush(fd, termios.TCIOFLUSH)

def send(s): os.write(fd, s.encode()+b"\r\n"); time.sleep(0.08)

def read_for(t):
    buf=b""; end=time.time()+t
    while time.time()<end:
        if select.select([fd],[],[],0.1)[0]:
            try: buf+=os.read(fd,4096)
            except BlockingIOError: pass
    return buf.decode("latin1","replace")

def snapshot():
    """Return dict name->int level from a fresh snapshot."""
    termios.tcflush(fd, termios.TCIFLUSH)
    send("s")
    txt = read_for(1.2)
    line = ""
    for l in txt.splitlines():
        if "[snap]" in l: line = l
    d = {}
    for m in re.finditer(r"(PA\d+)=(-?\d+)", line):
        d[m.group(1)] = int(m.group(2))
    return d

def set_level(dnum, want):
    """Force pin dnum (output) to level want (0/1), using toggle+readback."""
    name = NAME[dnum]
    for _ in range(3):
        snap = snapshot()
        cur = snap.get(name, None)
        if cur == want:
            return True
        send("t%d" % dnum)   # toggle
        time.sleep(0.15)
    snap = snapshot()
    return snap.get(name) == want

def release():
    send("r"); time.sleep(0.3)

print("=== MSS110 latching-relay combination tester ===")
print("For each pair it sets the two pins to OPPOSITE levels (SET), then swaps")
print("them (RESET). Listen for a relay CLICK at either step.\n")
input("Make sure no cu/screen is attached, then press Enter to begin... ")

try:
    for a_d, b_d in PAIRS:
        an, bn = NAME[a_d], NAME[b_d]
        print("\n==================================================")
        print(f"PAIR  {an}(D{a_d}) + {bn}(D{b_d})")
        release()
        # SET: a HIGH, b LOW
        ok1 = set_level(b_d, 0) and set_level(a_d, 1)
        print(f"  SET  -> {an}=HIGH {bn}=LOW   ({'ok' if ok1 else 'LEVEL SET FAILED'})  <-- listen for click")
        input("  [Enter] to apply RESET (swap)...")
        # RESET: a LOW, b HIGH
        ok2 = set_level(a_d, 0) and set_level(b_d, 1)
        print(f"  RESET-> {an}=LOW  {bn}=HIGH  ({'ok' if ok2 else 'LEVEL SET FAILED'})  <-- listen for click")
        ans = input("  Did EITHER step click the relay? [y to stop here / Enter for next pair] ")
        if ans.strip().lower() == "y":
            print(f"\n>>> Relay pair found: {an}(D{a_d}) + {bn}(D{b_d})")
            break
finally:
    release()
    os.close(fd)
    print("\nDone. All pins reset to input.")
