#pragma once

#include <catapult/crypto/KeyPair.h>
#include <catapult/ionet/PacketIo.h>
#include "catapult/types.h"
#include "catapult/ionet/ConnectionSecurityMode.h"

namespace catapult { namespace netio {

    /// Enumeration of verification results.
    enum class VerifyResult {
        /* An i/o error occurred while processing a server challenge request. */
        Io_Error_ServerChallengeRequest,

        /* An i/o error occurred while processing a server challenge response. */
	    Io_Error_ServerChallengeResponse,

        /* An i/o error occurred while processing a client challenge response. */
        Io_Error_ClientChallengeResponse,

        /* Peer sent malformed data. */
        Malformed_Data,

        /* Peer failed the challenge. */
        Failure_Challenge,

        /* Peer requested an unsupported connection (e.g. unsupported security mode). */
        Failure_Unsupported_Connection,

        /* Peer passed the challenge. */
        Success
    };

    /// Information about the verified node.
    struct VerifiedPeerInfo {
        /// Public key of the node.
        Key PublicKey;

        /// Security mode established.
        ionet::ConnectionSecurityMode SecurityMode;
    };

    using VerifyCallback = std::function<void(VerifyResult, const VerifiedPeerInfo)>;

    /// Attempts to verify a server (\a pServerIo) using \a serverPeerInfo and calls \a callback on completion.
    /// \a keyPair is used for responses from the client.
    void VerifyServer(
            const std::shared_ptr<ionet::PacketIo>& pServerIo,
            const VerifiedPeerInfo& serverPeerInfo,
            const crypto::KeyPair& keyPair,
            const VerifyCallback& callback);
}}