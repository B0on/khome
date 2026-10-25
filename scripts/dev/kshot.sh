#!/bin/sh
# usage: kshot.sh name  -> shots/name.png
cd "$(dirname "$0")"
mkdir -p shots; ssh -o BatchMode=yes kindle 'cat /dev/fb0' > shots/$1.raw 2>/dev/null
python3 -I fb2png.py shots/$1.raw shots/$1.pgm && sips -s format png shots/$1.pgm --out shots/$1.png >/dev/null
