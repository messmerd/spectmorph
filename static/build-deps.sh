#!/bin/bash

set -e

function autoconfconf {
echo "======= $(pwd) ======="
  CPPFLAGS="-I${PREFIX}/include${GLOBAL_CPPFLAGS:+ $GLOBAL_CPPFLAGS}" \
  CFLAGS="${GLOBAL_CFLAGS:+ $GLOBAL_CFLAGS}" \
  CXXFLAGS="${GLOBAL_CXXFLAGS:+ $GLOBAL_CXXFLAGS}" \
  LDFLAGS="${GLOBAL_LDFLAGS:+ $GLOBAL_LDFLAGS}" \
  ./configure --disable-dependency-tracking --prefix=$PREFIX $@
}

function autoconfbuild {
  autoconfconf $@
  make $MAKEFLAGS
  make install
}

SRCDIR=$PWD/src
BUILDD=$PWD/build

mkdir -p $SRCDIR $BUILDD

PREFIX=$PWD/prefix
GLOBAL_CFLAGS="-fPIC -DPIC"
GLOBAL_LDFLAGS="-L$PREFIX/lib"
MAKEFLAGS="-j$(nproc)"
PATH=$PWD/prefix/bin:$PATH
export PKG_CONFIG_PATH=$PREFIX/lib/pkgconfig:$PKG_CONFIG_PATH

cd $SRCDIR
apt-get source libogg
apt-get source libvorbis
apt-get source flac
apt-get source libsndfile
apt-get source zlib1g
apt-get source libffi
apt-get source glib2.0
apt-get source libpng-dev
apt-get source pixman
apt-get source freetype
apt-get source cairo
apt-get source fftw3

cd $SRCDIR/libogg-1.3.4
autoconfbuild --disable-shared

cd $SRCDIR/libvorbis-1.3.6
autoconfbuild --disable-shared

cd $SRCDIR/flac-1.3.3
./autogen.sh
autoconfbuild --disable-shared

cd $SRCDIR/libsndfile-1.0.28
autoconfbuild --disable-shared

cd $SRCDIR/zlib-1.2.11.dfsg
CFLAGS="${GLOBAL_CFLAGS}" \
LDFLAGS="${GLOBAL_LDFLAGS}" \
./configure --prefix=$PREFIX --static
make $MAKEFLAGS
make install

cd $SRCDIR/libffi-3.3
autoconfbuild --disable-shared

cd $SRCDIR/glib2.0-2.64.6
meson setup _build --prefix $PREFIX -D default_library=static
LIBRARY_PATH="$PREFIX/lib" meson install -C _build

cd $SRCDIR/libpng1.6-1.6.37
autoconfbuild --disable-shared

cd $SRCDIR/pixman-0.38.4
autoconfbuild --disable-shared

cd $SRCDIR/freetype-2.10.1
autoconfbuild --disable-shared --with-harfbuzz=no --with-png=no --with-bzip2=no

cd $SRCDIR/cairo-1.16.0
patch -p1 < ../../cairo-1.16.0-notest.diff
autoreconf -i # HACK (to avoid calling missing aclocal-1.15)
autoconfbuild --disable-shared

cd $SRCDIR/fftw3-3.3.8
autoconfbuild --disable-shared --disable-fortran --enable-single --enable-threads --with-combined-threads
