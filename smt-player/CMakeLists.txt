
cmake_minimum_required(VERSION 3.0.2)

#project name

PROJECT(smt-player)

SET(CMAKE_C_COMPILER "gcc")

#head file path
INCLUDE_DIRECTORIES(
    jni/gen/include
    /usr/include
)

#add link lirarries path
LINK_DIRECTORIES(
    jni/gen/lib
    /usr/lib/x86_64-linux-gnu
)

#source directory
AUX_SOURCE_DIRECTORY(./jni DIR_SRCS)

set(CMAKE_BUILD_TYPE "Debug")

SET(
	FFMPEG_SHARED_LIBS
	libavdevice.so
	libavfilter.so
	libavformat.so
	libavcodec.so
	libpostproc.so
	libswresample.so
	libswscale.so
	libavutil.so
)

SET(
	FFMPEG_STATIC_LIBS
	libavdevice.a
	libavfilter.a
	libavformat.a
	libavcodec.a
	libpostproc.a
	libswresample.a
	libswscale.a
	libavutil.a
)

SET(
	SYSTEM_LIBS
	-lpthread
	-lm
	-lz
	-lGL
	-lglut
	-lXv
	-lX11
	-lXext
	-lva
	-lva-x11
	-lxcb
	-lxcb-shm
	-lxcb-xfixes
	-lxcb-render
	-lxcb-shape
	-lxcb-shape
	-lasound
	-lx265
	-lx264
	-lSDL2
)

set(CMAKE_EXE_LINKER_FLAGS "-Wl,-rpath=${CMAKE_CURRENT_SOURCE_DIR}/gen/lib")
set(CMAKE_C_FLAGS "-g -Wall -fPIC -std=c99 -O0")

#add executable file
ADD_EXECUTABLE(smt-player ${DIR_SRCS})

#add library file
#ADD_LIBRARY(smt-player SHARED ${DIR_SRCS})

#add link library
TARGET_LINK_LIBRARIES(
    smt-player
    ${FFMPEG_STATIC_LIBS}
    ${SYSTEM_LIBS}
)



#add definition
add_definitions(
	-DSMT
	-DDebug
    -D_GNU_SOURCE=1 
    -D_REENTRANT
)