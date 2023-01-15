#!/bin/bash

set -ex

rm -rf ./build
mkdir ./build

cmake \
  -S . \
  -B ./build \
  -D CMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake \
  -D ANDROID_ABI=armeabi-v7a \
  -D ANDROID_PLATFORM=android-23 \

cmake --build ./build --config Release

rm -rf ipk/build
mkdir -p ipk/build

cp -r ipk/control ipk/build
cp -r ipk/debian-binary ipk/build
tar -czf ipk/build/control.tar.gz -C ipk/build/control .

mkdir -p ipk/build/data
mkdir -p ipk/build/data/opt/moonlight

cp -r ./build/dji-moonlight-shim ipk/build/data/opt/moonlight
cp -r ./assets ipk/build/data/opt/moonlight

tar -czf ipk/build/data.tar.gz -C ipk/build/data .


VERSION=$(cat ipk/control/control | grep Version | cut -d' ' -f2)
ARCH=$(cat ipk/control/control | grep Architecture | cut -d' ' -f2)
PKG_NAME=dji-moonlight-shim_${VERSION}_${ARCH}.ipk

tar -czf ipk/build/$PKG_NAME -C ipk/build debian-binary control.tar.gz data.tar.gz
