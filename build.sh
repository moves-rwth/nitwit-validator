#!/usr/bin/env bash

# Usage: ./build.sh -v/--version
#        ./build.sh
#        ./build.sh -debug


if [[ $1 == "-v" || $1 == "--version" ]]
then
  git rev-parse --short HEAD && exit 0
fi

if [[ $1 == "-debug" ]]
then
  # debug
  if [[ ! -d "cmake-build-debug" ]]
  then
    mkdir "cmake-build-debug" || exit 1
  fi
  cd cmake-build-debug || exit 1;
  cmake -DCMAKE_BUILD_TYPE=Debug .. || exit 1
  make -j8
  cd ..
  if [[ ! -d "bin" ]]
  then
    mkdir bin
  fi
  cp cmake-build-debug/nitwit32 bin/.
  cp cmake-build-debug/nitwit64 bin/.
else
  # release
  if [[ ! -d "cmake-build-release" ]]
  then
    mkdir "cmake-build-release" || exit 1
  fi
  cd cmake-build-release || exit 1;
  cmake -DCMAKE_BUILD_TYPE=Release .. || exit 1
  make -j8
  cd ..
  if [[ ! -d "bin" ]]
  then
    mkdir bin
  fi

  # Copy Files
  cp cmake-build-release/nitwit32 bin/.
  cp cmake-build-release/nitwit64 bin/.

  # Submission Archive
  mkdir -p submission/nitwit/bin
  mkdir -p submission/nitwit/picoc

  cp cmake-build-release/nitwit32 submission/nitwit/bin/
  cp cmake-build-release/nitwit64 submission/nitwit/bin/
  cp LICENSE submission/nitwit/
  cp picoc/LICENSE submission/nitwit/picoc/
  cp nitwit.sh submission/nitwit/
  cp README.md submission/nitwit/

  chmod +x submission/nitwit/bin/nitwit32
  chmod +x submission/nitwit/bin/nitwit64
  chmod +x submission/nitwit/nitwit.sh

  pushd submission
  zip -r val_nitwit.zip nitwit
  popd
fi