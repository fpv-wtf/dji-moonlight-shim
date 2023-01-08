#!/bin/bash

set -ex

rm -rf ./build
mkdir ./build

cmake \
  -S . \
  -B ./build \
  -D CMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
  -D ANDROID_ABI=armeabi-v7a \
  -D ANDROID_PLATFORM=android-23 \
  -D CMAKE_PREFIX_PATH=$HOME/libzmq/install/lib/cmake

cmake --build ./build --config Release

adb push ./build/dji-moonlight-shim /tmp

adb push ./assets /tmp/assets

adb shell "setprop persist.dji.storage.exportable 0"
adb shell "setprop dji.glasses_wm150_service 0"
adb shell "chmod +x /tmp/dji-moonlight-shim"
adb shell "cd /tmp; LD_LIBRARY_PATH=/tmp ./dji-moonlight-shim"
