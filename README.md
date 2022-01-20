## Audio & Video Player Project
inspired by Android NuPlayer

also use some project from Chromium to simplify cross-playform issues in the future

tools:
```
use chromium_tools.py from vsyf/chromium_tools to install chromium tools
```

Download:
```
gclient config --unmanaged git@github.com:vsyf/avp.git --name src
gclient sync -j16
```

build:
```
gn gen out/Default --export-compile-commands
ninja -C out/Default
```
