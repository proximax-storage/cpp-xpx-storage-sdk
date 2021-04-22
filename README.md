# cpp-xpx-storage-sdk

git clone git@github.com:proximax-storage/libtorrent.git
git submodule update --init
cd libtorrent
mkdir bin
cmake -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_STANDARD=17 -DSIRIUS_DRIVE="ON" -G Ninja ..
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=14 -DSIRIUS_DRIVE="ON" -G Ninja ..
#does not work cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_STANDARD=17 -Dbuild_examples="ON" -G Ninja ..
ninja
