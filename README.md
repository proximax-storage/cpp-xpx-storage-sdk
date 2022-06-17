# Get cpp-xpx-storage-sdk

```shell
git clone git@github.com:proximax-storage/cpp-xpx-storage-sdk.git
cd cpp-xpx-storage-sdk
git fetch --all
git checkout develop # or another
git submodule update --init --recursive --remote
```

## Build for Linux

### Build Libtorrent

```shell
cd libtorrent
mkdir _build && cd _build

cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 .. && make

cd ../..
```

### Build rpclib

```shell
cd rpclib
mkdir build && cd build
cmake .. && cmake --build .
#or cmake -DRPCLIB_ENABLE_LOGGING=ON .. && cmake --build .

cd ../..
```

### Build cpp-xpx-storage-sdk

```shell
mkdir _build && cd _build
cmake -DBOOST_ROOT=PATH/TO/BOOST/boost-build-1.71.0 .. && make
cd ..
```

## Build for Windows

IMPORTANT: For all commands, run cmd as an administrator.

### Install MinGW-W64 ###
If you do not have MinGW-W64 installed, download the 12.1.0 MSVCRT runtime version from here https://winlibs.com/
Extract the zip file to your C: drive, then add C:\mingw64\bin to PATH in system variables.

###  Install Boost 1.79.0

Download boost zip folder from https://www.boost.org/users/history/version_1_79_0.html
Extract to C: drive.

Now go to C: drive using Command Prompt, then run:

```shell
cd boost_1_79_0
bootstrap gcc
b2
b2 --build-dir=build/x64 address-model=64 threading=multi --build-type=complete --stagedir=./stage/x64 -j 4
```

Add the following to your system variables:

```shell
BOOST_BUILD_PATH    "C:\boost_1_79_0\tools\build"
BOOST_INCLUDEDIR    "C:\boost_1_79_0"
BOOST_LIBRARYDIR    "C:\boost_1_79_0\stage\x64\lib"
BOOST_ROOT          "C:\boost_1_79_0"
```

### Build Libtorrent

From cmd, go to the cpp-xpx-storage-sdk directory, then do the following:

```shell
cd libtorrent
mkdir _build
cd _build
cmake -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=20 ..
mingw32-make -j 6
```

### Build Rpclib

From cmd, go to the cpp-xpx-storage-sdk directory, then do the following:

```shell
cd cpp-xpx-rpclib
mkdir _build
cd _build
cmake -G "MinGW Makefiles" -DCMAKE_CXX_STANDARD=14 ..
mingw32-make -j 6
```

### Build cpp-xpx-storage-sdk

From cmd, go to the cpp-xpx-storage-sdk directory, then do the following:

```shell
cd cpp-xpx-storage-sdk
mkdir _build
cd _build
cmake -G "MinGW Makefiles" ..
mingw32-make -j 6
```

