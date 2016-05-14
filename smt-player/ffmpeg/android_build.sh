#!/bin/bash 
if [ $NDK_VERSION = "r10e" ]; then
SDK_VERSION=18
elif [ $NDK_VERSION = "r11c" ]; then
SDK_VERSION=23
fi
 
SYSTEMROOT=$NDK_HOME/platforms/android-$SDK_VERSION/arch-arm/  
TOOLCHAIN=$NDK_HOME/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64  

echo $SYSTEMROOT
echo $TOOLCHAIN
  
function build_one  
{ 
mkdir -p $PREFIX 
./configure \
    --prefix=$PREFIX \
    --enable-shared \
    --disable-static \
    --disable-doc \
    --disable-ffserver \
    --enable-cross-compile \
    --cross-prefix=$TOOLCHAIN/bin/arm-linux-androideabi- \
    --target-os=android \
    --arch=arm \
    --sysroot=$SYSTEMROOT \
    --extra-cflags="-Os -fpic $ADDI_CFLAGS" \
    --extra-ldflags="$ADDI_LDFLAGS" \
    $ADDITIONAL_CONFIGURE_FLAG
}  
CPU=arm  
PREFIX=$(pwd | xargs dirname)/android/jni/gen
ADDI_CFLAGS="-marm -DSMT_ANDROID"  
build_one  

