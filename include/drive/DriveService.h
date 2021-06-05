/**
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
**/

#include "drive/Utils.h"
#include "drive/FlatDrive.h"
#include "drive/Session.h"
#include <mutex>

#include <libtorrent/alert_types.hpp>
#include <filesystem>

namespace sirius { namespace drive {

constexpr auto Replicator_Host = "127.0.0.1:";

#ifndef CATAPULT_THROW_INVALID_ARGUMENT_1
#define CATAPULT_THROW_INVALID_ARGUMENT_1(msg,key) \
        std::cout << std::endl << msg << ": " << key; \
        throw std::runtime_error(msg);
#endif

struct DriveServiceConfig {
public:
    /// Replicator port.
    std::string Port;

    /// Storage directory.
    std::string StorageDirectory;

    /// Storage sandbox directory.
    std::string SandboxDirectory;
};

class DriveService : public std::enable_shared_from_this<DriveService> {
public:
    DriveService(const DriveServiceConfig& config ) : m_config(std::move(config)) {
        m_pSession = sirius::drive::createDefaultSession(Replicator_Host + m_config.Port, [port=m_config.Port](const lt::alert* pAlert) {
            if ( pAlert->type() == lt::listen_failed_alert::alert_type ) {
                LOG( "Replicator session alert: " << pAlert->message() );
                LOG( "Port is busy?: " << port );
            }
        });
    }

    ~DriveService() {}

    std::string addDrive(const Key& driveKey, size_t driveSize)
    {
        LOG( "adding drive " << driveKey );

        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_drives.find(driveKey) != m_drives.end()) {
            return "drive already added";
        }

        m_drives[driveKey] = sirius::drive::createDefaultFlatDrive(
                session(),
                m_config.StorageDirectory,
                m_config.SandboxDirectory,
                arrayToString(driveKey),
                driveSize);

        //TODO
        std::shared_ptr<sirius::drive::FlatDrive> pDrive;
        {
            if ( auto driveIt = m_drives.find(driveKey); driveIt != m_drives.end() )
            {
                pDrive = driveIt->second;
            }
            else {
                throw std::runtime_error("drive not found");
            }
        }

        pDrive->rootDriveHash();

        return "";
    }

    std::string removeDrive(const Key& driveKey)
    {
        LOG( "removing drive " << driveKey );

        std::unique_lock<std::mutex> lock(m_mutex);

        if (m_drives.find(driveKey) == m_drives.end())
            return "drive not found";

        m_drives.erase(driveKey);
        return "";
    }

    InfoHash getRootHash(const Key& driveKey)
    {
        LOG( "getRootHash " << driveKey );

        std::shared_ptr<sirius::drive::FlatDrive> pDrive;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            if ( auto driveIt = m_drives.find(driveKey); driveIt != m_drives.end() )
            {
                pDrive = driveIt->second;
            }
            else {
                throw std::runtime_error("drive not found");
            }
        }

        return pDrive->rootDriveHash();
    }

    std::string modify(const Key& driveKey, const InfoHash& infoHash, const DriveModifyHandler& handler )
    {
        LOG( "drive modification:\ndrive: " << driveKey << "\n info hash: " << infoHash );

        std::shared_ptr<sirius::drive::FlatDrive> pDrive;
        {
            const std::unique_lock<std::mutex> lock(m_mutex);
            if ( auto driveIt = m_drives.find(driveKey); driveIt != m_drives.end() )
            {
                pDrive = driveIt->second;
            }
            else {
                return "drive not found";
            }
        }

        pDrive->startModifyDrive( infoHash, handler );
        return "";
    }

private:
    std::shared_ptr<sirius::drive::Session> session() {
        return m_pSession;
    }

private:
    std::shared_ptr<sirius::drive::Session> m_pSession;
    std::map<Key, std::shared_ptr<sirius::drive::FlatDrive>> m_drives;
    std::mutex m_mutex;

    DriveServiceConfig m_config;
};


}}
