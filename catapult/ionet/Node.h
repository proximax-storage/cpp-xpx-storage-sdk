#pragma once

#include <string>
#include "catapult/types.h"

namespace catapult { namespace ionet {

    /// A node's publicly accessible endpoint.
    struct NodeEndpoint {
        /// Host.
        std::string Host;

        /// Port.
        unsigned short Port;
    };


    /// A node in the catapult network.
    struct Node {
    public:
        /// Creates a default node.
        Node(){}

        /// Creates a node around a unique identifier (\a identityKey) with \a endpoint and \a metadata.
        Node(const Key& identityKey, const NodeEndpoint& endpoint)
            : m_identityKey(identityKey)
            , m_endpoint(endpoint) {
        }

    public:
        /// Gets the unique identifier (a public key).
        const Key& identityKey() const {
            return m_identityKey;
        }

        /// Gets the endpoint.
        const NodeEndpoint& endpoint() const {
            return m_endpoint;
        }

    private:
        Key m_identityKey;
        NodeEndpoint m_endpoint;
    };
}}