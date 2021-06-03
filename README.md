# Get cpp-xpx-storage-sdk

git clone git@github.com:proximax-storage/cpp-xpx-storage-sdk.git
cd cpp-xpx-storage-sdk
git submodule update --init
git fetch origin
git checkout develop # or another

# Build Libtorrent

cd libtorrent
git fetch origin
git checkout develop # or another

mkdir _build
cd _build

cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -G Ninja ..
#or cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=17 -G Xcode .. 
cd ../..

# Build rpclib
cd ../rpclib
mkdir _build
cd _build
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

