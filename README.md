# Get cpp-xpx-storage-sdk

```shell
git clone git@github.com:proximax-storage/cpp-xpx-storage-sdk.git
cd cpp-xpx-storage-sdk
git fetch --all
git checkout develop # or another
git submodule init && git submodule update --recursive --remote
```

## Build Libtorrent

```shell
cd libtorrent
mkdir _build && cd _build
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -DBOOST_ROOT=PATH/TO/BOOST/boost-build-1.71.0 ..

or with CLION\`s cmake, i.e:
/snap/clion/152/bin/cmake/linux/bin/cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -DBOOST_ROOT=PATH/TO/BOOST/boost-build-1.71.0 ..

make -j 4
or cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=17 ..

cd ../..
```

## Build rpclib

```shell
cd ../rpclib
mkdir build && cd build
cmake ..
#or cmake -DRPCLIB_ENABLE_LOGGING=ON ..

cmake --build .
cd ../..
```

# Build cpp-xpx-storage-sdk

```shell
mkdir _build && cd _build
cmake ..
make -j 4
cd ..
```
