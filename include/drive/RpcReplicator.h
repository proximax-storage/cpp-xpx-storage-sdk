/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "drive/log.h"
#include "DriveService.h"
#include "RpcTypes.h"
#include "rpc/server.h"
#include "rpc/this_handler.h"

#include <future>


namespace sirius { namespace drive {
//
// RpcReplicator
//
class RpcReplicator
{
    std::string m_replicatorRootFolder;
    std::string m_sandboxRootFolder;

    std::map<InfoHash,std::shared_ptr<FlatDrive>>   m_driveMap;
    std::mutex                                      m_mutex;

    std::shared_ptr<DriveService>                   m_driveService;

    std::shared_ptr<rpc::server>                    m_rpcServer;

public:

    RpcReplicator( std::string         replicatorPort,
                   const std::string&  replicatorRootFolder,
                   const std::string&  sandboxRootFolder,
                   int                 rpcPort )
            :
            m_replicatorRootFolder( replicatorRootFolder ),
            m_sandboxRootFolder( sandboxRootFolder )
    {
        DriveServiceConfig config{ replicatorPort, replicatorRootFolder, sandboxRootFolder };
        m_driveService = std::make_shared<DriveService>( config );

        m_rpcServer = std::make_shared<rpc::server>("127.0.0.1",rpcPort);

        //
        // addDrive
        //
        m_rpcServer->bind("addDrive", [driveService=m_driveService] ( const std::array<uint8_t,32>& driveKey, size_t driveSize ) {
            auto reply = driveService->addDrive( reinterpret_cast<const sirius::Key&>(driveKey), driveSize );
            return reply;
        });

        //
        // removeDrive
        //
        m_rpcServer->bind("removeDrive", [driveService=m_driveService] ( const std::array<uint8_t,32>& driveKey ) {
            auto reply = driveService->removeDrive( reinterpret_cast<const sirius::Key&>(driveKey) );
            return reply;
        });

        //
        // getRootHash
        //
        m_rpcServer->bind("getRootHash", [driveService=m_driveService] ( const std::array<uint8_t,32>& driveKey ) {

            try {
                InfoHash hash = driveService->getRootHash( reinterpret_cast<const sirius::Key&>(driveKey) );

                ResultWithInfoHash result{ hash.array(), "" };

                return result;
            }
            catch( std::runtime_error &err) {
                ResultWithInfoHash result;
                result.m_error = err.what();
                return result;
            }
        });

        //
        // modify
        //
        m_rpcServer->bind("modify",
              [driveService=m_driveService] ( const std::array<uint8_t,32>& driveKey, const std::array<uint8_t,32>& hash )
        {
            auto drivePubKey = reinterpret_cast<const sirius::Key&>(driveKey);
            auto infoHash    = reinterpret_cast<const InfoHash&>(hash);
            std::promise<ResultWithModifyStatus> promise;
            auto future = promise.get_future();

            driveService->modify( drivePubKey, infoHash, [&promise,driveKey=drivePubKey] (
                                                            sirius::drive::modify_status::code code,
                                                            sirius::drive::InfoHash rootHash,
                                                            std::string error )
            {
                switch (code)
                {
                    case sirius::drive::modify_status::update_completed: {
                        LOG( " drive update completed:\n drive key: " << driveKey << "\n root hash: " << sirius::drive::toString(rootHash) );
                        ResultWithModifyStatus result{sirius::drive::modify_status::update_completed,""};
                        promise.set_value( result );
                        break;
                    }
                    case sirius::drive::modify_status::sandbox_root_hash: {
                        LOG( " drive modified in sandbox:\n drive key: " << driveKey << "\n root hash: " << sirius::drive::toString(rootHash) );
                        break;
                    }
                    case sirius::drive::modify_status::failed: {
                        LOG_ERR( " drive modification failed:\n drive key: " << driveKey << "\n error: " << error );
                        ResultWithModifyStatus result{sirius::drive::modify_status::failed,error};
                        promise.set_value( result );
                        break;
                    }
                    case sirius::drive::modify_status::broken: {
                        LOG_ERR( " drive modification aborted:\n drive key: " << driveKey );
                        ResultWithModifyStatus result{sirius::drive::modify_status::broken,""};
                        promise.set_value( result );
                        return;
                    }
                }
            });
            rpc::this_handler().respond( future.get() );
        });
    }

    void runRpcServer()
    {
        m_rpcServer->run();
    }

private:

    void replicatorDownloadHandler ( modify_status::code code, InfoHash /*resultRootInfoHash*/, std::string error )
    {
        if ( code == modify_status::update_completed )
        {
            LOG( "@ update_completed: " );
        }
        else if ( code == modify_status::sandbox_root_hash )
        {
            LOG( "@ sandbox calculated" );
        }
        else
        {
            LOG( "ERROR: " << error );
        }
    }

    static void sessionErrorHandler( const lt::alert* alert)
    {
        if ( alert->type() == lt::listen_failed_alert::alert_type )
        {
            std::cerr << alert->message() << std::endl << std::flush;
            exit(-1);
        }
    }

};

}}
