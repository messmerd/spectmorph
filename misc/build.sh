#!/bin/bash
set -Eeuo pipefail

build()
{
  if [ -f "./configure" ]; then
    make uninstall
    make distclean
  fi
  echo "###############################################################################"
  echo "# BUILD TESTS :"
  echo "#   CC=$CC CXX=$CXX "
  echo "#   ./autogen.sh $@"
  echo "###############################################################################"
  $CXX --version | sed '/^[[:space:]]*$/d;s/^/#   /'
  echo "###############################################################################"
  ./autogen.sh "$@"
  make -j `nproc` V=1
  make -j `nproc` check
  make install
}

# Tests using gcc
export CC=gcc CXX=g++

build --enable-asan --enable-debug-cxx

build
lv2lint http://spectmorph.org/plugins/spectmorph
make -j `nproc` distcheck

# Tests clang
export CC=clang CXX=clang++

build
lv2lint http://spectmorph.org/plugins/spectmorph
