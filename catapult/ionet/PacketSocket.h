/**
*** Copyright (c) 2016-present,
*** Jaguar0625, gimre, BloodyRookie, Tech Bureau, Corp. All rights reserved.
***
*** This file is part of Catapult.
***
*** Catapult is free software: you can redistribute it and/or modify
*** it under the terms of the GNU Lesser General Public License as published by
*** the Free Software Foundation, either version 3 of the License, or
*** (at your option) any later version.
***
*** Catapult is distributed in the hope that it will be useful,
*** but WITHOUT ANY WARRANTY; without even the implied warranty of
*** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*** GNU Lesser General Public License for more details.
***
*** You should have received a copy of the GNU Lesser General Public License
*** along with Catapult. If not, see <http://www.gnu.org/licenses/>.
**/

#pragma once
#include "BatchPacketReader.h"
#include "ConnectResult.h"
#include "IoTypes.h"
#include "PacketSocketOptions.h"

namespace catapult {
	namespace ionet {
		struct NodeEndpoint;
		struct Packet;
	}
}

namespace catapult { namespace ionet {

	/// An asio socket wrapper that natively supports packets.
	/// This wrapper is threadsafe but does not prevent interleaving reads or writes.
	class PacketSocket : public PacketIo, public BatchPacketReader {
	public:
		/// Statistics about a socket.
		struct Stats {
			/// \c true if the socket is open.
			bool IsOpen;

			/// Number of unprocessed bytes.
			size_t NumUnprocessedBytes;
		};

		using StatsCallback = consumer<const Stats&>;

	public:
		virtual ~PacketSocket() = default;

	public:
		/// Retrieves statistics about this socket and passes them to \a callback.
		virtual void stats(const StatsCallback& callback) = 0;

		/// Closes the socket.
		virtual void close() = 0;

		/// Gets a buffered interface to the packet socket.
		virtual std::shared_ptr<PacketIo> buffered() = 0;
	};

	// region Accept

	/// Result of a packet socket accept operation.
	class AcceptedPacketSocketInfo {
	public:
		/// Creates an empty info.
		AcceptedPacketSocketInfo()
		{}

		/// Creates an info around \a host and \a pPacketSocket.
		explicit AcceptedPacketSocketInfo(const std::string& host, const std::shared_ptr<PacketSocket>& pPacketSocket)
				: m_host(host)
				, m_pPacketSocket(pPacketSocket)
		{}

	public:
		/// Gets the host.
		const std::string& host() const {
			return m_host;
		}

		/// Gets the socket.
		const std::shared_ptr<PacketSocket>& socket() const {
			return m_pPacketSocket;
		}

	public:
		/// Returns \c true if this info is not empty.
		explicit operator bool() const {
			return !!m_pPacketSocket;
		}

	private:
		std::string m_host;
		std::shared_ptr<PacketSocket> m_pPacketSocket;
	};

	/// Callback for configuring a socket before initiating an accept.
	using ConfigureSocketCallback = consumer<socket&>;

	/// Callback for an accepted socket.
	using AcceptCallback = consumer<const AcceptedPacketSocketInfo&>;

	/// Accepts a connection using \a acceptor and calls \a accept on completion configuring the socket with \a options.
	void Accept(boost::asio::ip::tcp::acceptor& acceptor, const PacketSocketOptions& options, const AcceptCallback& accept);

	/// Accepts a connection using \a acceptor and calls \a accept on completion configuring the socket with \a options.
	/// \a configureSocket is called before starting the accept to allow custom configuration of asio sockets.
	/// \note User callbacks passed to the accepted socket are serialized.
	void Accept(
			boost::asio::ip::tcp::acceptor& acceptor,
			const PacketSocketOptions& options,
			const ConfigureSocketCallback& configureSocket,
			const AcceptCallback& accept);

	// endregion

	// region Connect

	/// Callback for a connected socket.
	using ConnectCallback = consumer<ConnectResult, const std::shared_ptr<PacketSocket>&>;

	/// Attempts to connect a socket to the specified \a endpoint using \a ioContext and calls \a callback on
	/// completion configuring the socket with \a options. The returned function can be used to cancel the connect.
	/// \note User callbacks passed to the connected socket are serialized.
	action Connect(
			boost::asio::io_context& ioContext,
			const PacketSocketOptions& options,
			const NodeEndpoint& endpoint,
			const ConnectCallback& callback);

	// endregion
}}
