/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <filesystem>
#include <iostream>
#include <fstream>
#include <vector>
#include <set>
#include <list>
#include <variant>
#include <memory>
#include <cstring>

#include <thread>
#include <shared_mutex>
#include <future>
#include <condition_variable>

// libtorrent
#include <libtorrent/alert.hpp>
#include <libtorrent/alert_types.hpp>
#include <libtorrent/session.hpp>
#include <libtorrent/magnet_uri.hpp>
#include <libtorrent/hex.hpp>
#include <libtorrent/create_torrent.hpp>
#include <libtorrent/torrent_flags.hpp>
#include <libtorrent/extensions/ut_metadata.hpp>

// boost
#include <boost/uuid/uuid.hpp>
#include <boost/uuid/uuid_generators.hpp>
#include <boost/uuid/uuid_io.hpp>
#include <boost/asio/ip/tcp.hpp>
#include "boost/date_time/posix_time/posix_time.hpp"

// cereal
//#include <cereal/types/vector.hpp>
//#include <cereal/types/array.hpp>
//#include <cereal/types/memory.hpp>
//#include <cereal/archives/binary.hpp>
//#include <cereal/archives/portable_binary.hpp>

namespace fs = std::filesystem;

