function build
{
./configure --prefix=$PREFIX --enable-shared --enable-static --enable-debug --enable-libx264 --enable-libx265 --enable-gpl --enable-nonfree --extra-ldflags=-Wl,-rpath=$PREFIX/lib
make uninstall
make clean
make
make install
}

PREFIX=$(pwd | xargs dirname)/jni/gen
mkdir -p $PREFIX
build
