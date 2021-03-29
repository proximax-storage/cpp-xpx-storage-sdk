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

#include "ionet/Node.h"
#include "model/Address.h"
#include "model/NetworkInfo.h"
#include <cctype>

namespace sirius { namespace ionet {

	namespace {
		void MakePrintable(std::string& str) {
			for (auto& ch : str)
				ch = std::isprint(ch) ? ch : '?';
		}

		std::string GetPrintableName(const Key& identityKey, const NodeEndpoint& endpoint, const NodeMetadata& metadata) {
			std::ostringstream printableName;
			if (metadata.Name.empty())
				printableName << model::AddressToString(model::PublicKeyToAddress(identityKey, metadata.NetworkIdentifier));
			else
				printableName << metadata.Name;

			if (!endpoint.Host.empty())
				printableName << " @ " + endpoint.Host << ":" << endpoint.Port;

			return printableName.str();
		}
	}

	Node::Node() : Node(Key(), NodeEndpoint(), NodeMetadata())
	{}

    Node::Node(const Key& identityKey, const NodeEndpoint& endpoint)
            : m_identityKey(identityKey)
            , m_endpoint(endpoint)
            , m_metadata(NodeMetadata()) {
	    m_printableName = GetPrintableName(m_identityKey, m_endpoint, m_metadata);
    }

	Node::Node(const Key& identityKey, const NodeEndpoint& endpoint, const NodeMetadata& metadata)
			: m_identityKey(identityKey)
			, m_endpoint(endpoint)
			, m_metadata(metadata) {
		MakePrintable(m_metadata.Name);
		MakePrintable(m_endpoint.Host);
		m_printableName = GetPrintableName(m_identityKey, m_endpoint, m_metadata);
	}

	const Key& Node::identityKey() const {
		return m_identityKey;
	}

	const NodeEndpoint& Node::endpoint() const {
		return m_endpoint;
	}

	const NodeMetadata& Node::metadata() const {
		return m_metadata;
	}

	bool Node::operator==(const Node& rhs) const {
		return m_identityKey == rhs.m_identityKey && m_metadata.NetworkIdentifier == rhs.m_metadata.NetworkIdentifier;
	}

	bool Node::operator!=(const Node& rhs) const {
		return !(*this == rhs);
	}

	std::ostream& operator<<(std::ostream& out, const Node& node) {
		out << node.m_printableName;
		return out;
	}
}}
