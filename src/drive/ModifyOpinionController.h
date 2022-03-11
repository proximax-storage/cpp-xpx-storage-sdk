/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include "drive/FlatDrive.h"
#include "drive/Replicator.h"
#include "Session.h"
#include "BackgroundExecutor.h"
#include "DriveParams.h"
#include "drive/Utils.h"
#include "drive/log.h"

#include <boost/multiprecision/cpp_int.hpp>
#include <numeric>

namespace sirius::drive {

class RestartValueSerializer;
class ThreadManager;

class ModifyOpinionController
{

    using uint128_t = boost::multiprecision::uint128_t;

private:

    Key                                         m_driveKey;

    Key                                         m_clientKey;

    ReplicatorInt&                                 m_replicator;
    const RestartValueSerializer&               m_serializer;
    ThreadManager&                              m_threadManager;

    // It is needed for right calculation of my 'modify' opinion
    std::optional<std::array<uint8_t, 32>>      m_opinionTrafficTx; // (***)
    uint64_t                                    m_approvedExpectedCumulativeDownload;
//    uint64_t                                    m_accountedCumulativeDownload; // (***)
    std::map<std::array<uint8_t,32>, uint64_t>  m_approvedCumulativeUploads; // (***)
    std::map<std::array<uint8_t,32>, uint64_t>  m_notApprovedCumulativeUploads; // (***)

    std::thread::id     m_dbgThreadId;
    std::string         m_dbgOurPeerName;

public:

    ModifyOpinionController(
            const Key& driveKey,
            const Key& client,
            ReplicatorInt& replicator,
            const RestartValueSerializer& serializer,
            ThreadManager& threadManager,
            uint64_t expectedCumulativeDownload,
            const std::string& dbgOurPeerName )
        :
              m_driveKey( driveKey )
            , m_clientKey( client )
            , m_replicator( replicator )
            , m_serializer( serializer )
            , m_threadManager( threadManager )
            , m_approvedExpectedCumulativeDownload( expectedCumulativeDownload )
            , m_dbgThreadId( std::this_thread::get_id())
            , m_dbgOurPeerName( dbgOurPeerName )
    {}

    void initialize()
    {
        DBG_BG_THREAD

        // We save 'AccountedUploads' because they will be needed after restart
        //
        m_serializer.loadRestartValue( m_approvedCumulativeUploads,      "approvedAccountedUploads" );
        m_serializer.loadRestartValue( m_notApprovedCumulativeUploads,  "notApprovedAccountedUploads" );
    }

    void increaseApprovedExpectedCumulativeDownload( uint64_t add )
    {
        DBG_MAIN_THREAD

        m_approvedExpectedCumulativeDownload += add;
    }

    std::optional<Hash256> opinionTrafficTx()
    {
        DBG_MAIN_THREAD

        return m_opinionTrafficTx;
    }

    void setOpinionTrafficTx(const Hash256& identifier)
    {
        DBG_MAIN_THREAD

        m_opinionTrafficTx = identifier.array();
    }

    void updateCumulativeUploads( const ReplicatorList&         replicators,
                                  uint64_t                      addCumulativeDownload,
                                  const std::function<void()>&  callback )
    {
        DBG_MAIN_THREAD

        const auto &modifyTrafficMap = m_replicator.getMyDownloadOpinion(*m_opinionTrafficTx)
                .m_modifyTrafficMap;

        std::map<std::array<uint8_t,32>, uint64_t> currentUploads;
        for (const auto &replicatorIt : replicators)
        {
            // get data size received from 'replicatorIt.m_publicKey'
            if (auto it = modifyTrafficMap.find(replicatorIt.array());
            it != modifyTrafficMap.end())
            {
                currentUploads[replicatorIt.array()] = it->second.m_receivedSize;
            } else
            {
                currentUploads[replicatorIt.array()] = 0;
            }
        }

        if (auto it = modifyTrafficMap.find(m_clientKey.array());
        it != modifyTrafficMap.end())
        {
            currentUploads[m_clientKey.array()] = it->second.m_receivedSize;
        } else
        {
            currentUploads[m_clientKey.array()] = 0;
        }

        auto accountedCumulativeDownload = std::accumulate( m_notApprovedCumulativeUploads.begin(),
                                                            m_notApprovedCumulativeUploads.end(), 0,
                                                            []( const auto& sum, const auto& item )
                                                            {
                                                                return sum + item.second;
                                                            } );
        auto expectedCumulativeDownload = m_approvedExpectedCumulativeDownload + addCumulativeDownload;

        _ASSERT( expectedCumulativeDownload > 0 )

        uint64_t targetSize = expectedCumulativeDownload - accountedCumulativeDownload;
        normalizeUploads(currentUploads, targetSize);
        m_replicator.removeModifyDriveInfo( *m_opinionTrafficTx );
        m_opinionTrafficTx.reset();

        for (const auto&[uploaderKey, bytes]: currentUploads)
        {
            if (m_notApprovedCumulativeUploads.find(uploaderKey) == m_notApprovedCumulativeUploads.end())
            {
                m_notApprovedCumulativeUploads[uploaderKey] = 0;
            }
            m_notApprovedCumulativeUploads[uploaderKey] += bytes;
        }

        m_threadManager.executeOnBackgroundThread([=, this] {
            m_serializer.saveRestartValue( m_notApprovedCumulativeUploads, "notApprovedAccountedUploads" );
            m_threadManager.executeOnSessionThread([=] {
                callback();
            });
        });
    }

    void fillOpinion( std::vector<KeyAndBytes>& replicatorsUploads )
    {
        DBG_MAIN_THREAD

        _ASSERT ( replicatorsUploads.empty() )

        for ( const auto&[key, bytes] : m_notApprovedCumulativeUploads )
        {
            replicatorsUploads.push_back( {key, bytes} );
        }
    }

    void approveCumulativeUploads( const std::function<void()>& callback )
    {
        DBG_MAIN_THREAD

        m_approvedCumulativeUploads = m_notApprovedCumulativeUploads;
        m_approvedExpectedCumulativeDownload = std::accumulate( m_approvedCumulativeUploads.begin(),
                                                                m_approvedCumulativeUploads.end(), 0,
                                                                []( const auto& sum, const auto& item )
                                                                {
                                                                    return sum + item.second;
                                                                } );

        m_threadManager.executeOnBackgroundThread( [=, this]
        {
            m_serializer.saveRestartValue( m_approvedCumulativeUploads, "approvedCumulativeUploads" );

            m_threadManager.executeOnSessionThread( [=]
            {
                callback();
            } );
        } );
    }

    void disapproveCumulativeUploads( const std::function<void()>& callback )
    {

        DBG_MAIN_THREAD

        // We have already taken into account information
        // about uploads of the modification to be canceled;
        auto trafficIdentifierHasValue = m_opinionTrafficTx.has_value();
        if ( trafficIdentifierHasValue )
        {
            m_replicator.removeModifyDriveInfo( *m_opinionTrafficTx );
            m_opinionTrafficTx.reset();
        }

        m_notApprovedCumulativeUploads = m_approvedCumulativeUploads;

        m_threadManager.executeOnBackgroundThread( [=, this]
            {
               m_serializer.saveRestartValue( m_notApprovedCumulativeUploads, "notApprovedAccountedUploads" );
               m_threadManager.executeOnSessionThread( [=]
               {
                   callback();
               } );
            } );
    }

private:

    void normalizeUploads(std::map<std::array<uint8_t,32>, uint64_t>& modificationUploads, uint64_t targetSum)
    {
        DBG_MAIN_THREAD

        _ASSERT(modificationUploads.contains(m_clientKey.array()))

        uint128_t longTargetSum = targetSum;
        uint128_t sumBefore = std::accumulate(modificationUploads.begin(),
                                              modificationUploads.end(),
                                              0,
                                              [] (const uint64_t& value, const std::pair<Key, int>& p)
                                              { return value + p.second; }
                                              );

        uint64_t sumAfter = 0;

        if ( sumBefore > 0 )
        {
            for ( auto& [key, uploadBytes]: modificationUploads ) {
                if ( key != m_clientKey.array() )
                {
                    auto longUploadBytes = (uploadBytes * longTargetSum) / sumBefore;
                    uploadBytes = longUploadBytes.convert_to<uint64_t>();
                    sumAfter += uploadBytes;
                }
            }
            modificationUploads[m_clientKey.array()] = targetSum - sumAfter;
        }
        else
        {
            modificationUploads[m_clientKey.array()] = targetSum;
        }
    }
};

} //namespace sirius::drive {
