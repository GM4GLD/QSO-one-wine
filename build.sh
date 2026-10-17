#!/bin/sh
# Rebuild all artifacts. Requires mingw-w64 toolchain (x86_64-w64-mingw32-gcc).
set -e
cd "$(dirname "$0")"

x86_64-w64-mingw32-gcc -shared -O2 -o mf-shim/mf.dll \
    mf-shim/mfshim.c mf-shim/mfshim.def \
    -lole32 -lmfplat -lmfuuid -luuid -lwinpthread

x86_64-w64-mingw32-gcc -shared -O2 \
    -o notif-stub/flutter_local_notifications_windows.dll \
    notif-stub/notifstub.c

x86_64-w64-mingw32-gcc -O0 -g -o probe/capprobe.exe probe/capprobe.c \
    -lole32 -lmfplat -lmfreadwrite -lmf -lmfuuid -luuid -loleaut32

echo "Built: mf-shim/mf.dll, notif-stub/flutter_local_notifications_windows.dll, probe/capprobe.exe"
echo "Deploy: cp mf-shim/mf.dll notif-stub/flutter_local_notifications_windows.dll ~/Downloads/qsoone/"
