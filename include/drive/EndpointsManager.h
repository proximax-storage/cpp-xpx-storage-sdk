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
    std::optional<boost::asio::ip::udp::endpoint> m_endpoint;
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
    std::array<uint8_t, sizeof( boost::asio::ip::udp::endpoint )> m_endpoint;
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
    std::array<uint8_t, sizeof( boost::asio::ip::udp::endpoint )> m_endpoint;
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
    std::map<Key, boost::asio::ip::udp::endpoint> m_unknownEndpointsMap;

    const crypto::KeyPair& m_keyPair;
    std::weak_ptr<Session> m_session;

    Timer m_externalPointUpdateTimer;

    std::optional<boost::asio::ip::udp::endpoint> m_myExternalEndpoint;
    std::optional<ExternalEndpointRequest> m_myExternalEndpointRequest;
    std::vector<ReplicatorInfo> m_bootstraps;

    const int m_standardExternalEndpointDelayMs = 1000 * 60 * 60;
    const int m_noResponseExternalEndpointDelayMs = 1000 * 5;
    
    using EndpointHandler = std::function<void(const Key&,const std::optional<boost::asio::ip::udp::endpoint>&)>;
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

    void addEndpointQuery(const Key& key, bool shouldRequestEndpoint = true)
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

    void addEndpointQueries(const std::vector<Key>& keys, bool shouldRequestEndpoint = true)
    {
        for (const auto& key: keys)
        {
            addEndpointQuery(key, shouldRequestEndpoint);
        }
    }

    void onEndpointDiscovered(const Key& key, const std::optional<boost::asio::ip::udp::endpoint>& endpoint)
    {
        if ( endpoint && endpoint->address().to_v4() != boost::asio::ip::address_v4::any() )
        {
            _LOG("Update Endpoint of " << toString(key.array()) << " at " << endpoint->address().to_string() << " : " << endpoint->port())
        }
        else
        {
            _LOG_WARN("Update Endpoint: invalid address")
            return;
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

    std::optional<boost::asio::ip::udp::endpoint> getEndpoint(const Key& key)
    {
        auto it = m_endpointsMap.find(key);
        if (it != m_endpointsMap.end())
        {
            if ( it->second.m_endpoint )
            {
                _LOG("todo: getEndpoint: -> " << toString(key.array()) << " -> " << *it->second.m_endpoint )
            }

            if ( it->second.m_endpoint && it->second.m_endpoint->address().to_v4() != boost::asio::ip::address_v4::any() )
            {
                return boost::asio::ip::udp::endpoint{ it->second.m_endpoint->address(), it->second.m_endpoint->port() };
            }
        }

        return {};
    }

    void onMyEndpointResponse(const ExternalEndpointResponse& response)
    {
        _LOG("updateMyExternalEndpoint:")

        auto session = m_session.lock();
        if (!session)
        {
            _LOG( "!session" )
            return;
        }

        if ( !m_myExternalEndpointRequest ||
             m_myExternalEndpointRequest->m_challenge != response.m_challenge ||
             m_myExternalEndpointRequest->m_requestTo != response.m_requestTo )
        {
            _LOG( "Bad response: " << bool(!m_myExternalEndpointRequest) )
            _LOG( "Bad response (1): " << toString(m_myExternalEndpointRequest->m_challenge) << " " << toString(response.m_challenge) )
            _LOG( "Bad response (2): " << toString(m_myExternalEndpointRequest->m_requestTo) << " " << toString(response.m_requestTo) )
            return;
        }

        if (!response.Verify())
        {
            _LOG( "Bad response verification" )
            return;
        }

        auto endpoint = *reinterpret_cast<const boost::asio::ip::udp::endpoint*>(&response.m_endpoint);
        
        if ( endpoint.address().to_v4() == boost::asio::ip::address_v4::any() )
        {
            _LOG_WARN("updateExternalEndpoint: invalid address " << endpoint.address() )
            return;
        }
        
        if (m_keyPair.publicKey().array() == response.m_requestTo)
        {
            _LOG_WARN("updateExternalEndpoint: invalid public key " << endpoint.address() )
            return;
        }

        _LOG("External Endpoint Discovered " << endpoint.address() << " " << std::dec << endpoint.port())
        
        if ( session->listeningPort() != endpoint.port() )
        {
            _LOG("Invalid port " << std::dec << endpoint.port() << " != " << session->listeningPort() )
            return;
        }
        
        _LOG("--1--")
        
        bool ipChanged = false;
        if (!m_myExternalEndpoint || m_myExternalEndpoint.value() != endpoint)
        {
            _LOG("--2--")
            ipChanged = true;
        }

        _LOG("--3--")
        m_myExternalEndpoint = endpoint;
        _LOG("--4--")

        if (ipChanged) {
            _LOG("--5--")
            // We expect that this operation does not take place too often
            // So the loop does not influence performance
            for (const auto&[key, point]: m_endpointsMap)
            {
                _LOG("--6--")
                sendHandshake(key);
            }
        }
        _LOG("--7--")

        m_myExternalEndpointRequest.reset();
        _LOG("--8--")
        session->announceMyIp(endpoint);
        _LOG("--9--")
    }

    const std::vector<ReplicatorInfo>& getBootstraps()
    {
        return m_bootstraps;
    }

private:

    void sendHandshake(const Key& to)
    {
        _LOG("sendHandshake:")

        SIRIUS_ASSERT(m_myExternalEndpoint)

        if (to.array() == m_keyPair.publicKey().array())
        {
            _LOG("Try to Send Handshake to --1--")
            return;
        }

        auto endpoint = getEndpoint(to);
        if ( !endpoint )
        {
            _LOG("Try to Send Handshake to --2--")
            return;
        }

        _LOG("Try to Send Handshake to --3--")
        DhtHandshake handshake;
        handshake.m_fromPublicKey = m_keyPair.publicKey().array();
        handshake.m_toPublicKey = to.array();
        handshake.m_endpoint = *reinterpret_cast<const std::array<uint8_t, sizeof(boost::asio::ip::udp::endpoint)>*>(m_myExternalEndpoint->data());
        handshake.Sign(m_keyPair);

        std::ostringstream os(std::ios::binary);
        cereal::PortableBinaryOutputArchive archive(os);
        archive(handshake);
        _LOG("Try to Send Handshake to --4--")

        auto session = m_session.lock();
        if (!session) {
            _LOG("Try to Send Handshake to --5--")
            return;
        }

        session->sendMessage("handshake", {endpoint->address(), endpoint->port()}, os.str());

        _LOG("Try to Send Handshake to " << toString(handshake.m_toPublicKey))
    }

    void onUpdateExternalEndpointTimerTick()
    {
        if (m_bootstraps.empty())
        {
            _LOG( "m_bootstraps.empty()" );
            // TODO maybe ask other nodes?
            return;
        }

        int bootstrapToAskIndex = rand() % (int)m_bootstraps.size();
        const auto& bootstrapToAsk = m_bootstraps[bootstrapToAskIndex];
        m_myExternalEndpointRequest =
                {
                        bootstrapToAsk.m_publicKey.array(),
                        randomByteArray<Hash256>().array()
                };

        std::ostringstream os(std::ios::binary);
        cereal::PortableBinaryOutputArchive archive(os);
        archive(*m_myExternalEndpointRequest);

        if ( auto session = m_session.lock(); !session )
        {
            _LOG( "!session" );
            return;
        }
        else
        {
            _LOG("session->sendMessage(endpoint_request)");
            
            session->sendMessage("endpoint_request", bootstrapToAsk.m_endpoint, os.str());
            
            _LOG("Requested External Endpoint from " << bootstrapToAsk.m_publicKey <<
                 " at " <<
                 bootstrapToAsk.m_endpoint.address().to_string() <<
                 " : " <<
                 bootstrapToAsk.m_endpoint.port() )
            
            m_externalPointUpdateTimer = session->startTimer(m_noResponseExternalEndpointDelayMs, [this]
                                                             {
                onUpdateExternalEndpointTimerTick();
            });
        }
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
