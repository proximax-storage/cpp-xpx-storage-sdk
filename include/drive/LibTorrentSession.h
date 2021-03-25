/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "ActionList.h"
#include <vector>
#include <boost/asio/ip/tcp.hpp>

#include "types.h"

using  tcp = boost::asio::ip::tcp;
using  endpoint_list = std::vector<tcp::endpoint>;

namespace sirius { namespace drive {

	class LibTorrentSession {
	public:

		virtual ~LibTorrentSession() = default;

		virtual void     endSession() = 0;

		virtual bool     addTorrentFileToSession(std::string torrentFilename,
												  std::string rootFolder,
												  endpoint_list = {}) = 0;

		virtual Hash256 addActionListToSession(const ActionList&,
												 const std::string& tmpFolderPath,
												 endpoint_list list = {}) = 0;

		virtual void     downloadFile(Hash256,
									   std::string outputFolder,
									   DownloadHandler,
									   endpoint_list list = {}) = 0;
	};

	Hash256 createTorrentFile(std::string pathToFolderOrFolder, std::string outputTorrentFilename = "");

	std::shared_ptr<LibTorrentSession> createDefaultLibTorrentWrapper(std::string address = "0.0.0.0:6881");
}}
