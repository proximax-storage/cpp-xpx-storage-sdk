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

### Install latest Visual Studio 2022

Download and install VS 2022 Community from https://visualstudio.microsoft.com/downloads/
In the Visual Studio Installer, check 'Desktop development with C++' and include the following modules:
	• MSVC v143 -VS 2022 C++ x64/x86 build tools
	• Just-In-Time debugger
	• C++ profiling tools
	• C++ CMake tools for Windows
	• C++ Clang tools for Windows
	• Windows 11 SDK (or windows 10 if you are using it)

Download VS Build Tools from https://aka.ms/vs/17/release/vs_BuildTools.exe
In the Visual Studio Installer, check 'Desktop development with C++' and include the following modules:
	• MSVC v143 -VS 2022 C++ x64/x86 build tools
	• C++ CMake tools for Windows
	• Testing tools core features - Build Tools
	• C++ AddressSanitizer
	• C++ Clang tools for Windows
	• Windows 11 SDK (or windows 10 if you are using it)

Add C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Current\Bin to PATH in the system variables.
Uninstall any older version of VS, then restart your machine.

###  Install Boost

Download boost zip folder from https://www.boost.org/users/download/
Extract to C: drive
In the directory C:\Users\User , create a file with the name user-config.jam
Append the following in the file:

```shell
using msvc ;
```
Now go to C: drive using Visual Studio 2022's Developer Command Prompt, then run:

```shell
cd boost_1_79_0
bootstrap
b2
b2 --build-dir=build/x64 address-model=64 threading=multi --build-type=complete --stagedir=./stage/x64 -j 4
```

Add the following to your system variables:

```shell
BOOST_BUILD_PATH    "C:\boost_1_79_0\tools\build"
BOOST_INCLUDEDIR    "C:\boost_1_79_0\boost"
BOOST_LIBRARYDIR    "C:\boost_1_79_0\stage\x64\lib"
BOOST_ROOT          "C:\boost_1_79_0"
```

### Build Libtorrent

From cmd, go to the cpp-xpx-storage-sdk directory, then do the following:

```shell
cd libtorrent
mkdir _build
cd _build
cmake -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=20 ..
msbuild ALL_BUILD.vcxproj
```

### Build Rpclib

From cmd, go to the cpp-xpx-storage-sdk directory, then do the following:

```shell
cd cpp-xpx-rpclib
mkdir build
cd build
cmake -G "Visual Studio 17 2022" -DCMAKE_CXX_STANDARD=14 ..
msbuild ALL_BUILD.vcxproj
```

