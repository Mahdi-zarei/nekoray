#!/bin/bash
set -e

cd libs

# 参数
if [ -z $cmake ]; then
  cmake="cmake"
fi
if [ -z $deps ]; then
  deps="deps"
fi

# libs/deps/...
mkdir -p $deps
cd $deps
INSTALL_PREFIX=$PWD/built
rm -rf $INSTALL_PREFIX
mkdir -p $INSTALL_PREFIX

#### clean ####
clean() {
  rm -rf dl.zip yaml-* zxing-* protobuf curl cpr libpsl* zlib vcpkg
}

#### ZXing v2.2.0 ####
curl -L -o dl.zip https://github.com/nu-book/zxing-cpp/archive/refs/tags/v2.2.0.zip
unzip dl.zip

cd zxing-*
mkdir -p build
cd build

$cmake .. -GNinja -DBUILD_SHARED_LIBS=OFF -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=OFF -DBUILD_BLACKBOX_TESTS=OFF -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX
ninja && ninja install

cd ../..

#### yaml-cpp ####
curl -L -o dl.zip https://github.com/jbeder/yaml-cpp/archive/refs/tags/yaml-cpp-0.7.0.zip
unzip dl.zip

cd yaml-*
mkdir -p build
cd build

$cmake .. -GNinja -DBUILD_SHARED_LIBS=OFF -DBUILD_TESTING=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX
ninja && ninja install

cd ../..

#### protobuf ####
git clone --recurse-submodules -b v28.2 --depth 1 --shallow-submodules https://github.com/protocolbuffers/protobuf

mkdir -p protobuf/build
cd protobuf/build

$cmake .. -GNinja \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -Dprotobuf_MSVC_STATIC_RUNTIME=OFF \
  -Dprotobuf_BUILD_TESTS=OFF \
  -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX \
  -DCMAKE_CXX_STANDARD=17
ninja && ninja install

cd ../..

if [[ "$(uname -s)" == *"NT"* ]]; then
  git clone https://github.com/Microsoft/vcpkg.git
  cd vcpkg
  export VCPKG_BUILD_TYPE="release"
  export VCPKG_LIBRARY_LINKAGE="static"
  ./bootstrap-vcpkg.sh
  ./vcpkg integrate install
  ./vcpkg install curl:x64-windows-static --x-install-root=$INSTALL_PREFIX

  cd ..

  git clone https://github.com/libcpr/cpr.git
  cd cpr
  git checkout bb01c8db702fb41e5497aee9c0559ddf4bf13749
  sed -i 's/find_package(CURL COMPONENTS HTTP HTTPS)/find_package(CURL REQUIRED)/g' CMakeLists.txt
  mkdir build && cd build
  cmake -GNinja .. -DCMAKE_BUILD_TYPE=Release -DCPR_USE_SYSTEM_CURL=ON -DBUILD_SHARED_LIBS=OFF -DCURL_STATICLIB=ON -DCURL_LIBRARY=../../built/x64-windows-static/lib -DCURL_INCLUDE_DIR=../../built/x64-windows-static/include -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX
  ninja && ninja install

  cd ../..

  else
    git clone https://github.com/curl/curl.git
    cd curl
    git checkout 7eb8c048470ed2cc14dca75be9c1cdae7ac8498b
    mkdir build && cd build
    cmake -GNinja .. -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX -DCURL_STATICLIB=ON -DUSE_LIBIDN2=OFF
    ninja && ninja install

    cd ../..

    git clone https://github.com/libcpr/cpr.git
    cd cpr
    git checkout bb01c8db702fb41e5497aee9c0559ddf4bf13749
    mkdir build && cd build
    cmake -GNinja .. -DCMAKE_BUILD_TYPE=Release -DCPR_USE_SYSTEM_CURL=ON -DBUILD_SHARED_LIBS=OFF -DCURL_STATICLIB=ON -DCMAKE_INSTALL_PREFIX=$INSTALL_PREFIX
    ninja && ninja install

    cd ../..
fi

####
clean
