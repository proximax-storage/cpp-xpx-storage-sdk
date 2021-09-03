/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "rpc/msgpack/adaptor/define_decl.hpp"
#include "rpc/msgpack.hpp"


namespace sirius::drive::types {

        struct ResultWithInfoHash {
            std::array<uint8_t,32>  m_rootHash;
            std::string             m_error;
            MSGPACK_DEFINE_ARRAY(m_rootHash,m_error);
        };

        struct ResultWithModifyStatus {
            int                     m_modifyStatus;
            std::string             m_error;
            MSGPACK_DEFINE_ARRAY(m_modifyStatus,m_error);
        };

        struct DownloadChannelInfo {
            std::vector<Key> getClientsPublicKeys() const {
                std::vector<Key> clientsPublicKeys(m_clientsPublicKeys.size());
                std::copy(m_clientsPublicKeys.begin(), m_clientsPublicKeys.end(), clientsPublicKeys.begin());

                return clientsPublicKeys;
            }

            void setClientsPublicKeys(const std::vector<Key>& clientsPublicKeys) {
                for (const Key& key : clientsPublicKeys) {
                    m_clientsPublicKeys.emplace_back(key.array());
                }
            }

            ReplicatorList getReplicators() const {
                ReplicatorList replicators;
                for (const ReplicatorInfo& replicatorInfo : m_replicators) {
                    boost::asio::ip::address address = boost::asio::ip::address::from_string(replicatorInfo.m_address);
                    replicators.emplace_back( drive::ReplicatorInfo{ {
                        address,
                        replicatorInfo.m_port
                    },  replicatorInfo.m_publicKey } );
                }

                return replicators;
            }

            void setReplicators(const ReplicatorList& replicators) {
                for (const drive::ReplicatorInfo& replicatorInfo : replicators) {
                    ReplicatorInfo info;
                    info.m_address = replicatorInfo.m_endpoint.address().to_string();
                    info.m_port = replicatorInfo.m_endpoint.port();

                    std::copy(replicatorInfo.m_publicKey.begin(), replicatorInfo.m_publicKey.end(), info.m_publicKey.begin());
                    m_replicators.emplace_back(info);
                }
            }

            struct ReplicatorInfo {
                std::array<uint8_t, 32> m_publicKey;
                std::string             m_address;
                unsigned short          m_port;
                MSGPACK_DEFINE_ARRAY(m_publicKey, m_address, m_port);
            };

            std::array<uint8_t, 32>                 m_channelKey;
            size_t                                  m_prepaidDownloadSize;
            std::vector<ReplicatorInfo>             m_replicators;
            std::vector<std::array<uint8_t, 32>>    m_clientsPublicKeys;
            MSGPACK_DEFINE_ARRAY(m_channelKey, m_prepaidDownloadSize, m_replicators, m_clientsPublicKeys);
        };
}
