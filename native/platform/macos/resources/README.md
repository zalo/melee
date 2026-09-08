# App artwork

`SSBM.png` is the Super Smash Bros. Melee logo, sourced from
https://www.mariowiki.com/File:SSBM.png (original uploaded July 8, 2013):
https://www.mariowiki.com/images/d/d9/SSBM.png

The game logo belongs to its respective rights holders. The source labels it
as a copyrighted game logo, not an open-source asset. It is used here at the
project owner's request for app identification. No game disc data is included.

`MeleeNative.icns` packages that logo on a dark macOS icon tile. Regenerate with:

```sh
swift native/tools/make_icon.swift native/platform/macos/resources/SSBM.png /tmp/melee-native.iconset
iconutil -c icns /tmp/melee-native.iconset -o native/platform/macos/resources/MeleeNative.icns
```
