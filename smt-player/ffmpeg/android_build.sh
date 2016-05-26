#!/bin/bash 

function smt_configure  
{ 
#mkdir -p $PREFIX
./configure $CONFIG_FLAG --extra-cflags="$CONFIG_EXTRA_FLAG" --extra-ldflags="$CONFIG_LD_FLAG -llog"
}  
 
SYSTEMROOT=$NDK_HOME/platforms/android-$SDK_VERSION/arch-arm/  
TOOLCHAIN=$NDK_HOME/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64  

echo $SYSTEMROOT
echo $TOOLCHAIN

#CPU=arm  
#PREFIX=$(pwd | xargs dirname)/SMTPlayer/player/src/main/jni/gen
CONFIG_FLAG="--prefix=$SYSTEMROOT/usr
	--sysroot=$SYSTEMROOT
	--disable-shared \
	--enable-static \
	--disable-doc \
	--disable-ffserver \
	--enable-cross-compile \
	--cross-prefix=$TOOLCHAIN/bin/arm-linux-androideabi- \
	--target-os=linux \
	--enable-pic"
if [ "$1" = "armv5" ]; then
CONFIG_FLAG="$CONFIG_FLAG --arch=arm"
CONFIG_EXTRA_FLAG="-march=armv5te -mtune=arm9tdmi -msoft-float"
echo "arm core = armv5"
elif [ "$1" = "armv7a" ]; then
CONFIG_FLAG="$CONFIG_FLAG --arch=arm --cpu=cortex-a8 --enable-neon --enable-thumb"
CONFIG_EXTRA_FLAG="-march=armv7-a -mcpu=cortex-a8 -mfpu=vfpv3-d16 -mfloat-abi=softfp"
CONFIG_LD_FLAG="-Wl,--fix-cortex-a8"
echo "arm core = armv7a"
else
echo "please specify the ABI (armv5 or armv7a)"
exit 0
fi

if [ "$2" = "debug" ]; then
CONFIG_FLAG="$CONFIG_FLAG --enable-debug --disable-optimizations --disable-small"
fi

smt_configure  

