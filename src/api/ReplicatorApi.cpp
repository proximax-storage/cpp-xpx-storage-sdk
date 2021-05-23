/**
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
**/

#include "RemoteRequestDispatcher.h"
#include "api/ReplicatorApi.h"
#include "api/Packets.h"
#include "utils/Casting.h"

namespace sirius { namespace api {

	namespace {
		template<ionet::PacketType RequestPacketType, ionet::PacketType ResponsePacketType>
		struct FileDownloadTraits {
		public:
			using ResultType = std::vector<std::string>;
			static constexpr auto Packet_Type = ResponsePacketType;
			static constexpr auto Friendly_Name = "file download";

			static auto CreateRequestPacketPayload(const Key& driveKey, const std::vector<std::string>& fileNames) {
				uint32_t payloadSize = 0u;
				for (const auto& fileName : fileNames)
					payloadSize += sizeof(uint16_t) + fileName.size();
				auto pPacket = ionet::CreateSharedPacket<FileDownloadPacket<RequestPacketType>>(payloadSize);
				pPacket->DriveKey = driveKey;
				pPacket->FileCount = utils::checked_cast<size_t, uint32_t>(fileNames.size());
				auto* pPayload = reinterpret_cast<char*>(pPacket.get() + 1);

				for (const auto& fileName : fileNames) {
					auto fileNameSize = fileName.size();
					*reinterpret_cast<uint16_t*>(pPayload) = utils::checked_cast<size_t, uint16_t>(fileNameSize);
					pPayload += sizeof(uint16_t);
					memcpy(pPayload, fileName.data(), fileNameSize);
					pPayload += fileNameSize;
				}

				return ionet::PacketPayload(pPacket);
			}

		public:
			bool tryParseResult(const ionet::Packet& packet, ResultType& result) const {
				const auto* pResponse = ionet::CoercePacket<FileDownloadPacket<ResponsePacketType>>(&packet);
				if (!pResponse)
					return false;

				result.clear();
				result.reserve(pResponse->FileCount);
				const auto* pPayload = reinterpret_cast<const char*>(pResponse + 1);
				for (auto i = 0u; i < pResponse->FileCount; ++i) {
					auto fileNameSize = *reinterpret_cast<const uint16_t*>(pPayload);
					pPayload += sizeof(uint16_t);
					std::string fileName(pPayload, fileNameSize);
					pPayload += fileNameSize;
					result.push_back(fileName);
				}

				return true;
			}
		};

		class DefaultReplicatorApi : public ReplicatorApi {
		private:
			template<typename TTraits>
			using FutureType = thread::future<typename TTraits::ResultType>;

		public:
			DefaultReplicatorApi(ionet::PacketIo& io)
				: m_impl(io)
			{}

		public:
			FutureType<FileDownloadTraits<ionet::PacketType::Start_Download_Files, ionet::PacketType::Start_Download_Files_Response>> startDownloadFiles(
					const Key& driveKey, const std::vector<std::string>& fileNames) const override {
				return m_impl.dispatch(FileDownloadTraits<ionet::PacketType::Start_Download_Files, ionet::PacketType::Start_Download_Files_Response>(), driveKey, fileNames);
			}

			FutureType<FileDownloadTraits<ionet::PacketType::Stop_Download_Files, ionet::PacketType::Stop_Download_Files_Response>> stopDownloadFiles(
					const Key& driveKey, const std::vector<std::string>& fileNames) const override {
				return m_impl.dispatch(FileDownloadTraits<ionet::PacketType::Stop_Download_Files, ionet::PacketType::Stop_Download_Files_Response>(), driveKey, fileNames);
			}

		private:
			mutable RemoteRequestDispatcher m_impl;
		};
	}

	std::unique_ptr<ReplicatorApi> CreateReplicatorApi(ionet::PacketIo& io) {
		return std::make_unique<DefaultReplicatorApi>(io);
	}
}}
