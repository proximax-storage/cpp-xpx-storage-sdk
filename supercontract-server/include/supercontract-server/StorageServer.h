/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <thread>
#include <storageServer.grpc.pb.h>
#include <grpcpp/completion_queue.h>
#include <grpcpp/server_builder.h>
#include <boost/asio/io_context.hpp>
#include <drive/ModificationsExecutor.h>
#include <drive/RPCService.h>

namespace sirius::drive::contract
{

class StorageServer
        : public RPCService
{

private:

    std::unique_ptr<grpc::ServerCompletionQueue> m_cq;
    storageServer::StorageServer::AsyncService m_service;
    std::weak_ptr<IOContextProvider> m_context;
    std::thread m_thread;
    std::shared_ptr<bool> m_serviceIsActive;
    std::weak_ptr<ModificationsExecutor> m_executor;

public:

    explicit StorageServer(std::weak_ptr<ModificationsExecutor> executor);

    void registerService( grpc::ServerBuilder& builder ) override;

    void run( std::weak_ptr<IOContextProvider> contextKeeper) override;

    ~StorageServer() override;

private:

    void waitForQueries();

    void registerSynchronizeStorage();

    void registerInitiateModifications();

    void registerInitiateSandboxModifications();

    void registerApplySandboxStorageModifications();

    void registerEvaluateStorageHash();

    void registerApplyStorageModifications();

    void registerOpenFile();

    void registerReadFile();

    void registerWriteFile();

    void registerCloseFile();

    void registerFlush();

    void registerGetAbsolutePath();

    void registerGetActualModificationId();

    void registerGetFilesystem();

    void registerDirectoryIteratorCreate();

    void registerDirectoryIteratorHasNext();

    void registerDirectoryIteratorNext();

    void registerDirectoryIteratorDestroy();

    void registerRemoveFilesystemEntry();

    void registerMoveFilesystemEntry();

    void registerCreateDirectories();

    void registerPathExist();

    void registerIsFile();

};

}