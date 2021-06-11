# Get cpp-xpx-storage-sdk

git clone git@github.com:proximax-storage/cpp-xpx-storage-sdk.git<br>
cd cpp-xpx-storage-sdk<br>
git submodule update --init --recursive<br>
git fetch origin<br>
git checkout alex-flat-drive # or 'develop'?<br>

# Build Libtorrent

cd libtorrent<br>
git submodule update --recursive --remote<br>
git fetch origin<br>
git checkout xpx-work # or 'develop'?<br>

mkdir _build<br>
cd _build<br>

cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -DBOOST_ROOT=PATH/TO/BOOST/boost-build-1.71.0 ..<br>
make -j 4<br>
cd ../..<br>

<br># or /snap/clion/152/bin/cmake/linux/bin/cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -DBOOST_ROOT=PATH/TO/BOOST/boost-build-1.71.0 ..<br>
<br># or cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=17 -G Xcode ..<br>

# Build rpclib
cd ../rpclib<br>
mkdir build<br>
cd build<br>
cmake ..<br> #or cmake -DRPCLIB_ENABLE_LOGGING=ON ..<br>
cmake --build .<br>
cd ../..<br>

# Build cpp-xpx-storage-sdk
mkdir _build<br>
cd _build<br>
cmake ..<br>
make<br>
cd ..<br>

