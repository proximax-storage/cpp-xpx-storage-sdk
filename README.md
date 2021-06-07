# Get cpp-xpx-storage-sdk

git clone git@github.com:proximax-storage/cpp-xpx-storage-sdk.git
cd cpp-xpx-storage-sdk
git submodule update --init
git fetch origin
git checkout develop # or another

# Build Libtorrent

cd libtorrent
git submodule init
git submodule update --recursive --remote
git fetch origin
git checkout develop # or another

mkdir _build
cd _build

cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -DBOOST_ROOT=PATH/TO/BOOST/boost-build-1.71.0 ..
# or /snap/clion/152/bin/cmake/linux/bin/cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -DBOOST_ROOT=PATH/TO/BOOST/boost-build-1.71.0 ..
make -j 4
#or cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=17 -G Xcode ..
cd ../..

# Build rpclib
cd ../rpclib
mkdir build
cd build
cmake ..
#or cmake -DRPCLIB_ENABLE_LOGGING=ON ..
cmake --build .
cd ../..

# Build cpp-xpx-storage-sdk
mkdir _build
cd _build
cmake ..
make
cd ..

