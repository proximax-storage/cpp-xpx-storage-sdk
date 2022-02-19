/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "drive/Replicator.h"
#include "ReplicatorInt.h"
#include "Session.h"
#include "drive/FlatDrive.h"
#include "drive/Utils.h"
//#include "ModifyOpinionController.h"

#include <cereal/types/vector.hpp>
#include <cereal/types/array.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/optional.hpp>
#include <cereal/archives/portable_binary.hpp>

#include <fstream>

#undef DBG_MAIN_THREAD
//#define DBG_MAIN_THREAD { assert( m_dbgThreadId == std::this_thread::get_id() ); }
#define DBG_MAIN_THREAD { _FUNC_ENTRY(); assert( m_dbgThreadId == std::this_thread::get_id() ); }
#define DBG_BG_THREAD { assert( m_dbgThreadId != std::this_thread::get_id() ); }

namespace sirius::drive {

namespace fs = std::filesystem;

class RestartValueSerializer
{

public:

    fs::path m_restartRootPath;

    std::thread::id m_dbgThreadId;
    std::string m_dbgOurPeerName;

    RestartValueSerializer(
            const fs::path& restartRootPath,
            const std::string& dbgOurPeerName )
            : m_restartRootPath( restartRootPath ), m_dbgThreadId( std::this_thread::get_id()),
              m_dbgOurPeerName( dbgOurPeerName )
    {}

    template<class T>
    void saveRestartValue( T& value, std::string path ) const
    {
        DBG_BG_THREAD

        _ASSERT( fs::exists( m_restartRootPath ))

        std::ostringstream os( std::ios::binary );
        cereal::PortableBinaryOutputArchive archive( os );
        archive( value );

        saveRestartData( m_restartRootPath / path, os.str());
    }

    template<class T>
    bool loadRestartValue( T& value, std::string path ) const
    {
        DBG_BG_THREAD

        std::string data;

        if ( !loadRestartData( m_restartRootPath / path, data ))
        {
            return false;
        }

        std::istringstream is( data, std::ios::binary );
        cereal::PortableBinaryInputArchive iarchive( is );
        iarchive( value );
        return true;
    }

private:

    void saveRestartData( std::string outputFile, const std::string data ) const
    {
        try
        {
            {
                std::ofstream fStream( outputFile + ".tmp", std::ios::binary );
                fStream << data;
            }
            _ASSERT( fs::exists( outputFile + ".tmp" ))
            std::error_code err;
            fs::remove( outputFile, err );
            fs::rename( outputFile + ".tmp", outputFile, err );
        }
        catch (const std::exception& ex)
        {
            _LOG_ERR( "exception during saveRestartData: " << ex.what());
        }
    }

    bool loadRestartData( std::string outputFile, std::string& data ) const
    {
        std::error_code err;

        if ( fs::exists( outputFile, err ))
        {
            std::ifstream ifStream( outputFile, std::ios::binary );
            if ( ifStream.is_open())
            {
                std::ostringstream os;
                os << ifStream.rdbuf();
                data = os.str();
                return true;
            }
        }

        if ( fs::exists( outputFile + ".tmp", err ))
        {
            std::ifstream ifStream( outputFile + ".tmp", std::ios::binary );
            if ( ifStream.is_open())
            {
                std::ostringstream os;
                os << ifStream.rdbuf();
                data = os.str();
                return true;
            }
        }

        //_LOG_WARN( "cannot loadRestartData: " << outputFile );

        return false;
    }
};

class FlatDrivePaths {
protected:
    FlatDrivePaths( const std::string&  replicatorRootFolder,
                    const std::string&      replicatorSandboxRootFolder,
                    const Key&              drivePubKey )
                    :
                    m_driveKey( drivePubKey ),
                    m_replicatorRoot( replicatorRootFolder ),
                    m_replicatorSandboxRoot( replicatorSandboxRootFolder )
                    {}

                    virtual~FlatDrivePaths() {}

public:

    const Key       m_driveKey;

private:

    const fs::path  m_replicatorRoot;
    const fs::path  m_replicatorSandboxRoot;

public:

    // Drive paths
    const fs::path  m_driveRootPath     = m_replicatorRoot / arrayToString(m_driveKey.array());
    const fs::path  m_driveFolder       = m_driveRootPath  / "drive";
    const fs::path  m_torrentFolder     = m_driveRootPath  / "torrent";
    const fs::path  m_fsTreeFile        = m_driveRootPath  / "fs_tree" / FS_TREE_FILE_NAME;
    const fs::path  m_fsTreeTorrent     = m_driveRootPath  / "fs_tree" / FS_TREE_FILE_NAME ".torrent";

    // Sandbox paths
    const fs::path  m_sandboxRootPath       = m_replicatorSandboxRoot / arrayToString(m_driveKey.array());
    const fs::path  m_sandboxFsTreeFile     = m_sandboxRootPath / FS_TREE_FILE_NAME;
    const fs::path  m_sandboxFsTreeTorrent  = m_sandboxRootPath / FS_TREE_FILE_NAME ".torrent";

    // Client data paths (received action list and files)
    const fs::path  m_clientDataFolder      = m_sandboxRootPath / "client-data";
    const fs::path  m_clientDriveFolder     = m_clientDataFolder / "drive";
    const fs::path  m_clientActionListFile  = m_clientDataFolder / "actionList.bin";

    // Restart data
    const fs::path  m_restartRootPath       = m_driveRootPath  / "restart-data";
    const fs::path  m_driveIsClosingPath    = m_driveRootPath  / "restart-data" / "drive-is-closing";
};

class ThreadManager
{

public:

    virtual ~ThreadManager() = default;

    virtual void executeOnSessionThread( const std::function<void()>& task ) = 0;

    virtual void executeOnBackgroundThread( const std::function<void()>& task ) = 0;
};

class DriveParams: public FlatDrivePaths, public ThreadManager
{

public:

    const Key m_driveOwner;
    
    const size_t   m_maxSize;
    const size_t   m_currentDriveSize = 0;

    std::weak_ptr<Session> m_session;

    // It is as 1-st parameter in functions of ReplicatorEventHandler (for debugging)
    ReplicatorInt& m_replicator;

    // Replicator event handlers
    ReplicatorEventHandler& m_eventHandler;
    DbgReplicatorEventHandler* m_dbgEventHandler = nullptr;

    // Serializer
    const RestartValueSerializer m_serializer;

    //
    // TorrentHandleMap is used to avoid adding torrents into session with the same hash
    // and for deleting unused files and torrents from session
    //
    std::map<InfoHash, UseTorrentInfo> m_torrentHandleMap;

    //
    // Drive state
    //

    InfoHash m_rootHash;

    // FsTree
    std::unique_ptr <FsTree> m_fsTree;
    lt_handle m_fsTreeLtHandle; // used for removing FsTree torrent from session

    // For debugging:
    const std::string                       m_dbgOurPeerName;
    const std::thread::id                   m_dbgThreadId;

protected:

    DriveParams(
            const Key&                  drivePubKey,
            const Key&                  driveOwner,
            size_t                      maxSize,
            std::shared_ptr<Session>    session,
            ReplicatorEventHandler&     eventHandler,
            ReplicatorInt&              replicator,
            DbgReplicatorEventHandler*  dbgEventHandler,
            const std::string&          replicatorRootFolder,
            const std::string&          replicatorSandboxRootFolder,
            const std::string&          dbgOurPeerName
        )
        : FlatDrivePaths( replicatorRootFolder, replicatorSandboxRootFolder, drivePubKey )
        , m_driveOwner(driveOwner)
        , m_maxSize(maxSize)
        , m_session( session )
        , m_replicator( replicator )
        , m_eventHandler( eventHandler )
        , m_dbgEventHandler( dbgEventHandler )
        , m_serializer(m_restartRootPath, dbgOurPeerName)
        , m_fsTree( std::make_unique<FsTree>() )
        , m_dbgOurPeerName( dbgOurPeerName )
        , m_dbgThreadId( std::this_thread::get_id())
    {}

    virtual ~DriveParams() = default;

public:

    virtual const ReplicatorList& getAllReplicators() const = 0;

    virtual void runNextTask() = 0;
};

//class ModifyOpinionController
//{
//public:
//
//    virtual ~ModifyOpinionController() = default;
//
//    virtual void initialize() = 0;
//
//    virtual std::optional<Hash256> opinionTrafficTx() = 0;
//
//    virtual void setOpinionTrafficTx( const Hash256& ) = 0;
//
//    virtual void approveCumulativeUploads( const std::function<void()>& callback ) = 0;
//
//    virtual void disapproveCumulativeUploads( const std::function<void()>& callback ) = 0;
//
//    virtual void
//    updateCumulativeUploads( const ReplicatorList& replicators, uint64_t addCumulativeDownload, const std::function<void()>& callback ) = 0;
//
//    virtual void fillOpinion( std::vector<KeyAndBytes>& replicatorsUploads,
//                              uint64_t& clientUploads ) = 0;
//
//    virtual void increaseApprovedExpectedCumulativeDownload( uint64_t ) = 0;
//};

}
