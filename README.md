# Get cpp-xpx-storage-sdk

```shell
git clone git@github.com:proximax-storage/cpp-xpx-storage-sdk.git
cd cpp-xpx-storage-sdk
git fetch --all
git checkout develop # or another
git submodule update --init --recursive --remote
```

## Build Libtorrent

```shell
cd libtorrent
mkdir _build && cd _build

cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 .. && make

cd ../..
```

## Build rpclib

```shell
cd rpclib
mkdir build && cd build
cmake .. && cmake --build .
#or cmake -DRPCLIB_ENABLE_LOGGING=ON .. && cmake --build .

cd ../..
```

# Build cpp-xpx-storage-sdk

```shell
mkdir _build && cd _build
cmake -DBOOST_ROOT=PATH/TO/BOOST/boost-build-1.71.0 .. && make
cd ..
```
