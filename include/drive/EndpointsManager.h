/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "drive/Session.h"

#include <cereal/archives/portable_binary.hpp>
#include <utility>

namespace sirius::drive
{

struct EndpointInformation
{
    std::optional<boost::asio::ip::tcp::endpoint> m_endpoint;
    Timer m_timer;
};

struct ExternalEndpointRequest
{

    std::array<uint8_t, 32> m_requestTo;
    std::array<uint8_t, 32> m_challenge;

    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_requestTo );
        arch( m_challenge );
    }
};

struct ExternalEndpointResponse
{

    std::array<uint8_t, 32> m_requestTo;
    std::array<uint8_t, 32> m_challenge;
    std::array<uint8_t, sizeof( boost::asio::ip::tcp::endpoint )> m_endpoint;
    Signature m_signature;

    void Sign( const crypto::KeyPair& keyPair )
    {
        crypto::Sign( keyPair,
                      {
                              utils::RawBuffer{m_challenge},
                              utils::RawBuffer{m_endpoint}
                      },
                      m_signature );
    }

    bool Verify() const
    {
        return crypto::Verify( m_requestTo,
                               {
                                       utils::RawBuffer{m_challenge},
                                       utils::RawBuffer{m_endpoint}
                               },
                               m_signature );
    }

    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_requestTo );
        arch( m_challenge );
        arch( m_endpoint );
        arch( cereal::binary_data( m_signature.data(), m_signature.size()));
    }
};

struct DhtHandshake
{
    std::array<uint8_t, 32> m_fromPublicKey;
    std::array<uint8_t, 32> m_toPublicKey;
    std::array<uint8_t, sizeof( boost::asio::ip::tcp::endpoint )> m_endpoint;
    Signature m_signature;

    void Sign( const crypto::KeyPair& keyPair )
    {
        crypto::Sign( keyPair,
                      {
                              utils::RawBuffer{m_toPublicKey},
                              utils::RawBuffer{m_endpoint}
                      },
                      m_signature );
    }

    bool Verify() const
    {
        return crypto::Verify( m_fromPublicKey,
                               {
                                       utils::RawBuffer{m_toPublicKey},
                                       utils::RawBuffer{m_endpoint}
                               },
                               m_signature );
    }


    template<class Archive>
    void serialize( Archive& arch )
    {
        arch( m_fromPublicKey );
        arch( m_toPublicKey );
        arch( m_endpoint );
        arch( cereal::binary_data( m_signature.data(), m_signature.size()));
    }
};

class EndpointsManager
{

private:

    std::map<Key, EndpointInformation> m_endpointsMap;
    std::map<Key, boost::asio::ip::tcp::endpoint> m_unknownEndpointsMap;

    const crypto::KeyPair& m_keyPair;
    std::weak_ptr<Session> m_session;

    Timer m_externalPointUpdateTimer;

    std::optional<boost::asio::ip::tcp::endpoint> m_externalEndpoint;
    std::optional<ExternalEndpointRequest> m_externalEndpointRequest;
    std::vector<ReplicatorInfo> m_bootstraps;

    const int m_standardExternalEndpointDelayMs = 1000 * 60 * 60;
    const int m_noResponseExternalEndpointDelayMs = 1000 * 5;
    
    using EndpointHandler = std::function<void(const Key&,const std::optional<boost::asio::ip::tcp::endpoint>&)>;
    std::optional<EndpointHandler> m_endpointHandler;
    std::string m_dbgOurPeerName = "noname";

public:

    EndpointsManager(const crypto::KeyPair& keyPair,
                     const std::vector<ReplicatorInfo>& bootstraps,
                     const std::string& dbgOurPeerName)
            : m_keyPair(keyPair), m_bootstraps(bootstraps), m_dbgOurPeerName(dbgOurPeerName)
    {
        std::erase_if( m_bootstraps, [this]( const auto& item )
        {
            return m_keyPair.publicKey() == item.m_publicKey;
        });

        for (const auto&[endpoint, key] : bootstraps)
        {
            m_endpointsMap[key] = {endpoint, {}};
        }
    }
    
    void setEndpointHandler( EndpointHandler endpointHandler )
    {
        m_endpointHandler = endpointHandler;
    }

    void start(std::weak_ptr<Session> session)
    {
        m_session = std::move(session);
        onUpdateExternalEndpointTimerTick();
    }

    void stop()
    {
        m_externalPointUpdateTimer.cancel();
        for (auto&[key, value]: m_endpointsMap)
        {
            value.m_timer.cancel();
        }
    }

    void addEndpointEntry(const Key& key, bool shouldRequestEndpoint = true)
    {
        {
            if (m_endpointsMap.contains(key) && !m_endpointsMap[key].m_endpoint && shouldRequestEndpoint)
            {
                requestEndpoint(key);
                return;
            }
        }

        if (m_unknownEndpointsMap.contains(key))
        {
            m_endpointsMap[key].m_endpoint = m_unknownEndpointsMap[key];
#ifdef UPDATE_ENDPOINTS_PERIODICALLY
            if ( auto session = m_session.lock(); session )
            {
                m_endpointsMap[key].m_timer = session->startTimer(m_standardExternalEndpointDelayMs, [this, key]
                {
                    requestEndpoint(key);
                });
            }
#endif
            m_unknownEndpointsMap.erase(key);
        } else
        {
            m_endpointsMap[key] = {};
            if (shouldRequestEndpoint)
            {
                // Now we should not request client's ip since it is not in dht
                requestEndpoint(key);
            }
        }
    }

    void addEndpointsEntries(const std::vector<Key>& keys, bool shouldRequestEndpoint = true)
    {
        for (const auto& key: keys)
        {
            addEndpointEntry(key, shouldRequestEndpoint);
        }
    }

    void updateEndpoint(const Key& key, const std::optional<boost::asio::ip::tcp::endpoint>& endpoint)
    {
        if (endpoint)
        {
            _LOG("Update Endpoint of " << toString(key.array()) << " at " << endpoint->address().to_string() << " : " << endpoint->port())
        }
        
        auto it = m_endpointsMap.find(key);
        if (it != m_endpointsMap.end())
        {
            if (endpoint)
            {
                if ( m_endpointHandler && it->second.m_endpoint != endpoint )
                {
                    _LOG("todo: m_endpointHandler: <- " << toString(it->first.array()) << " <- " << endpoint->address().to_string() << " : " << endpoint->port())
                    (*m_endpointHandler)( key, endpoint);
                }
                
                //----------------------------------------------------------------------------------------------
                // !!! set endpoint here !!!
                //----------------------------------------------------------------------------------------------
                _LOG("todo: addEndpoint: <- " << toString(it->first.array()) << " <- " << endpoint->address().to_string() << " : " << endpoint->port())
                it->second.m_endpoint = endpoint;
                
#ifdef UPDATE_ENDPOINTS_PERIODICALLY
                if ( auto session = m_session.lock(); session )
                {
                    it->second.m_timer = session->startTimer( m_standardExternalEndpointDelayMs, [this, key]
                    {
                        requestEndpoint(key);
                    });
                }
#else
                it->second.m_timer.cancel();
#endif
            } else
            {
                if (auto session = m_session.lock(); session)
                {
                    it->second.m_timer = session->startTimer(m_noResponseExternalEndpointDelayMs, [this, key]
                    {
                        requestEndpoint(key);
                    });
                }
            }
        } else
        {
            SIRIUS_ASSERT(endpoint)
            m_unknownEndpointsMap[key] = *endpoint;
        }
    }

    std::optional<boost::asio::ip::tcp::endpoint> getEndpoint(const Key& key)
    {
        auto it = m_endpointsMap.find(key);
        if (it != m_endpointsMap.end())
        {
            if ( it->second.m_endpoint )
            {
                _LOG("todo: getEndpoint: -> " << toString(key.array()) << " -> " << *it->second.m_endpoint )
            }

            return it->second.m_endpoint;
        }

        return {};
    }

    void updateExternalEndpoint(const ExternalEndpointResponse& response)
    {
        auto session = m_session.lock();
        if (!session)
        {
            return;
        }

        if (!m_externalEndpointRequest ||
            m_externalEndpointRequest->m_challenge != response.m_challenge ||
            m_externalEndpointRequest->m_requestTo != response.m_requestTo)
        {
            return;
        }

        if (!response.Verify())
        {
            return;
        }

        auto receivedEndpoint = *reinterpret_cast<const boost::asio::ip::tcp::endpoint*>(&response.m_endpoint);
        if (m_keyPair.publicKey().array() == response.m_requestTo)
        {
            return;
        }

        _LOG("External Endpoint Discovered " << receivedEndpoint.address() << " " << std::dec << receivedEndpoint.port())

        boost::asio::ip::tcp::endpoint externalEndpoint(receivedEndpoint.address(), receivedEndpoint.port());

        bool ipChanged = false;
        if (!m_externalEndpoint || m_externalEndpoint.value() != externalEndpoint)
        {
            ipChanged = true;
        }

        m_externalEndpoint = externalEndpoint;

        if (ipChanged) {
            // We expect that this operation does not take place too often
            // So the loop does not influence performance
            for (const auto&[key, point]: m_endpointsMap)
            {
                sendHandshake(key);
            }
        }

        m_externalEndpointRequest.reset();
        session->announceExternalAddress(externalEndpoint);

        m_externalPointUpdateTimer = session->startTimer(m_standardExternalEndpointDelayMs, [this]
        {
            onUpdateExternalEndpointTimerTick();
        });
    }

    const std::vector<ReplicatorInfo>& getBootstraps()
    {
        return m_bootstraps;
    }

private:

    void sendHandshake(const Key& to)
    {
        SIRIUS_ASSERT(m_externalEndpoint)

        if (to.array() == m_keyPair.publicKey().array())
        {
            return;
        }

        auto endpoint = getEndpoint(to);
        if ( !endpoint )
        {
            return;
        }

        DhtHandshake handshake;
        handshake.m_fromPublicKey = m_keyPair.publicKey().array();
        handshake.m_toPublicKey = to.array();
        handshake.m_endpoint = *reinterpret_cast<const std::array<uint8_t, sizeof(boost::asio::ip::tcp::endpoint)>*>(m_externalEndpoint->data());
        handshake.Sign(m_keyPair);

        std::ostringstream os(std::ios::binary);
        cereal::PortableBinaryOutputArchive archive(os);
        archive(handshake);

        auto session = m_session.lock();
        if (!session) {
            return;
        }

        session->sendMessage("handshake", {endpoint->address(), endpoint->port()}, os.str());

        _LOG("Try to Send Handshake to " << toString(handshake.m_toPublicKey))
    }

    void onUpdateExternalEndpointTimerTick()
    {
        if (m_bootstraps.empty())
        {
            // TODO maybe ask other nodes?
            return;
        }

        int bootstrapToAskIndex = rand() % (int)m_bootstraps.size();
        const auto& bootstrapToAsk = m_bootstraps[bootstrapToAskIndex];
        m_externalEndpointRequest =
                {
                        bootstrapToAsk.m_publicKey.array(),
                        randomByteArray<Hash256>().array()
                };

        std::ostringstream os(std::ios::binary);
        cereal::PortableBinaryOutputArchive archive(os);
        archive(*m_externalEndpointRequest);

        auto session = m_session.lock();
        if ( !session )
        {
            return;
        }

        auto endpoint = getEndpoint(bootstrapToAsk.m_publicKey);
        if (endpoint &&
            m_keyPair.publicKey().array() != m_externalEndpointRequest->m_requestTo &&
            m_keyPair.publicKey().array() != bootstrapToAsk.m_publicKey.array())
        {
            session->sendMessage("endpoint_request", {endpoint->address(), endpoint->port()}, os.str());

            _LOG("Requested External Endpoint from " <<
                    toString(bootstrapToAsk.m_publicKey.array()) <<
                    " at " <<
                    bootstrapToAsk.m_endpoint.address().to_string() <<
                    " : " << bootstrapToAsk.m_endpoint.port())
        }

        m_externalPointUpdateTimer = session->startTimer(m_noResponseExternalEndpointDelayMs, [this]
        {
            onUpdateExternalEndpointTimerTick();
        });
    }

    void requestEndpoint(const Key& key)
    {
        _LOG("Requested Endpoint of " << toString(key.array()))
        if (auto session = m_session.lock(); session)
        {
            session->findAddress(key);
        }
    }
};
}
