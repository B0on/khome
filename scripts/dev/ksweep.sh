#!/bin/sh
# usage: ksweep.sh x1 y1 x2 y2 [steps]
# Injects one straight drag as the kernel's evdev events on event1.
#
# The whole sequence is written in a single `cat` so press and release cannot
# straddle an ssh round trip, and slot 0 is selected explicitly so the events
# do not depend on which MT slot the previous touch left behind.
cd "$(dirname "$0")"
python3 -I swipegen.py "$@"
cat swipe.bin | ssh -o BatchMode=yes kindle 'mkdir -p /tmp/survey; cat > /tmp/survey/swipe.bin; cat /tmp/survey/swipe.bin > /dev/input/event1'
