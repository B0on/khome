#!/bin/sh
# usage: ktap.sh x y  (screen pixels)
cd "$(dirname "$0")"
python3 -I tapgen.py $1 $2
cat down.bin | ssh -o BatchMode=yes kindle 'mkdir -p /tmp/survey; cat > /tmp/survey/down.bin'
cat up.bin | ssh -o BatchMode=yes kindle 'mkdir -p /tmp/survey; cat > /tmp/survey/up.bin; cat /tmp/survey/down.bin > /dev/input/event1; usleep 120000; cat /tmp/survey/up.bin > /dev/input/event1'
