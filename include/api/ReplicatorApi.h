/**
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
**/

#pragma once
#include "thread/Future.h"
#include "types.h"

namespace sirius { namespace ionet { class PacketIo; } }

namespace sirius { namespace api {

	/// An api for making requests to replicators.
	class PLUGIN_API ReplicatorApi {
	public:
		ReplicatorApi() = default;
		virtual ~ReplicatorApi() = default;

	public:
		virtual thread::future<std::vector<std::string>> startDownloadFiles(const Key& driveKey, const std::vector<std::string>& fileNames) const = 0;
		virtual thread::future<std::vector<std::string>> stopDownloadFiles(const Key& driveKey, const std::vector<std::string>& fileNames) const = 0;
	};

	/// Creates replicator api for interacting with a replicator with the specified \a io.
	PLUGIN_API std::unique_ptr<ReplicatorApi> CreateReplicatorApi(ionet::PacketIo& io);
}}
