
cmake_minimum_required(VERSION 3.0.2)

#project name

PROJECT(smt-player)

#head file path
INCLUDE_DIRECTORIES(
    include
)

#add link lirarries path
LINK_DIRECTORIES(
    lib
)

#source directory
AUX_SOURCE_DIRECTORY(. DIR_SRCS)

#set environment variable
SET(
    SMT_HOME $(shell pwd)
)

SET(
	FFMPEG_LIBS
	libswresample.so
	libavcodec.so
	libavutil.so
	libavformat.so
	libavfilter.so
	libswscale.so
	libavdevice.so
)

#add executable file
ADD_EXECUTABLE(smt-player smt_player.c)

#add link library
TARGET_LINK_LIBRARIES(
    smt-player
    ${FFMPEG_LIBS}
)

#add definition
add_definitions(
	-Wall
	-Wl,-rpath=lib
)
