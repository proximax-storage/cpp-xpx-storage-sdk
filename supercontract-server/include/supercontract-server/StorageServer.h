/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <thread>
#include <storageServer.grpc.pb.h>
#include <grpcpp/completion_queue.h>
#include <boost/asio/io_context.hpp>
#include <drive/ModificationsExecutor.h>
#include <supercontract-server/AbstractSupercontractServer.h>

namespace sirius::drive::contract
{

class StorageServer
        : public AbstractSupercontractServer
{

private:

    std::string m_address;
    std::unique_ptr<grpc::ServerCompletionQueue> m_cq;
    storageServer::StorageServer::AsyncService m_service;
    std::unique_ptr<grpc::Server> m_server;
    std::weak_ptr<ContextKeeper> m_context;
    std::thread m_thread;
    std::shared_ptr<bool> m_serviceIsActive;
    std::weak_ptr<ModificationsExecutor> m_executor;

public:

    explicit StorageServer( std::string address );

    void run( std::weak_ptr<ContextKeeper> contextKeeper,
              std::weak_ptr<ModificationsExecutor> executor ) override;

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

    void registerGetFilesystem();

};

}