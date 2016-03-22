function build
{
./configure --prefix=$PREFIX --enable-shared --disable-static --enable-debug --enable-libx264 --enable-libx265 --enable-gpl --enable-nonfree --extra-ldflags=-Wl,-rpath=$PREFIX/lib
}

PREFIX=$(pwd | xargs dirname)/gen
build
