# Third-party notices

This repository contains or reproduces the following third-party material.
The full license text is kept with each component where noted.

## stb

- Files: `launcher/third_party/stb_image.h`, `launcher/third_party/stb_image_write.h`, `launcher/third_party/stb_truetype.h`
- License: public domain / MIT (dual), as stated at the end of each header.
- Provenance: `launcher/third_party/README.md`.

## openlipc

- Files: `kindle/fakekaf/openlipc.h`, `kindle/fakekaf/LICENSE.openlipc`
- License: MIT. Copyright (c) 2016 Arkadiusz Bokowy.
- The header notes that the LIPC interface itself is Amazon's; only the
  reverse-engineered declarations are covered by the MIT license.

## Noto fonts

- Files: `design/system/fonts/*.ttf`, `design/mockups/fonts/*.ttf`
- License: SIL Open Font License 1.1.
- License texts: `design/system/fonts/OFL-NotoSansJP.txt`,
  `design/system/fonts/OFL-NotoSerifJP.txt`,
  `design/mockups/fonts/OFL-NotoSansJP.txt`,
  `design/mockups/fonts/OFL-NotoSerifJP.txt`.

## Mozilla CA bundle

- File: `kindle/khome/cacert.pem`
- License: Mozilla Public License 2.0 (MPL 2.0).
- Source: Mozilla root certificates, extracted with `mk-ca-bundle.pl`; the
  file header records the bundle date and source URL.

## X11 / Xlib declarations

- Files: `kindle/xband/xband.c`, `kindle/xexpose/xexpose.c`
- License: X11 (MIT-style), for the public Xlib / `X11/X.h` declarations
  reproduced in these files (structures, constants, and function signatures).
- Xlib itself is loaded at runtime and is not bundled.

## Linux UAPI declarations

- File: `launcher/tests/stub/linux/input.h`
- License: GPL-2.0 WITH Linux-syscall-note, for the reproduced
  `struct input_event` and evdev constants.
- The device build uses the Linux UAPI headers provided by the toolchain
  (`linux/fb.h`, `linux/input.h`) and does not copy them into the repository.
