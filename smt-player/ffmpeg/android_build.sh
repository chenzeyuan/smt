#!/bin/bash 

function smt_configure  
{ 
mkdir -p $PREFIX
./configure $CONFIG_FLAG --extra-cflags="$CONFIG_EXTRA_FLAG" --extra-ldflags="$CONFIG_LD_FLAG"
}  

if [ $NDK_VERSION = "r10e" ]; then
SDK_VERSION=18
elif [ $NDK_VERSION = "r11c" ]; then
SDK_VERSION=23
fi
 
SYSTEMROOT=$NDK_HOME/platforms/android-$SDK_VERSION/arch-arm/  
TOOLCHAIN=$NDK_HOME/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64  

echo $SYSTEMROOT
echo $TOOLCHAIN

CPU=arm  
PREFIX=$(pwd | xargs dirname)/android/jni/gen
CONFIG_FLAG="--prefix=$PREFIX \
	--enable-shared \
	--disable-static \
	--disable-doc \
	--disable-ffserver \
	--enable-cross-compile \
	--cross-prefix=$TOOLCHAIN/bin/arm-linux-androideabi- \
	--target-os=android \
	--enable-pic \
	--sysroot=$SYSTEMROOT"
if [ $NDK_VERSION = "r10e" ]; then
CONFIG_FLAG="$CONFIG_FLAG --arch=arm"
CONFIG_EXTRA_FLAG="-march=armv5te -mtune=arm9tdmi -msoft-float"
echo "arm core = armv5"
elif [ $NDK_VERSION = "r11c" ]; then
CONFIG_FLAG="$CONFIG_FLAG --arch=arm --cpu=cortex-a8 --enable-neon --enable-thumb"
CONFIG_EXTRA_FLAG="-march=armv7-a -mcpu=cortex-a8 -mfpu=vfpv3-d16 -mfloat-abi=softfp"
CONFIG_LD_FLAG="-Wl,--fix-cortex-a8"
echo "arm core = armv7a"
fi

if [ "$1" = "debug" ]; then
CONFIG_FLAG="$CONFIG_FLAG --enable-debug --disable-optimizations --disable-small"
fi

smt_configure  

