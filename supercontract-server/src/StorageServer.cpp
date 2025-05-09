/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include <grpcpp/security/server_credentials.h>

#include <utility>
#include <supercontract-server/StorageServer.h>
#include "RPCTag.h"
#include "SynchronizeStorageRequestContext.h"
#include "InitiateModificationsRequestContext.h"
#include "InitiateSandboxModificationsRequestContext.h"
#include "ApplySandboxModificationsRequestContext.h"
#include "EvaluateStorageHashRequestContext.h"
#include "ApplyStorageModificationsRequestContext.h"
#include "OpenFileRequestContext.h"
#include "ReadFileRequestContext.h"
#include "WriteFileRequestContext.h"
#include "CloseFileRequestContext.h"
#include "FlushRequestContext.h"
#include "FileInfoRequestContext.h"
#include "FilesystemRequestContext.h"
#include "RemoveFilesystemEntryRequestContext.h"
#include "MoveFilesystemEntryRequestContext.h"
#include "CreateDirectoriesRequestContext.h"
#include "DirectoryIteratorCreateRequestContext.h"
#include "DirectoryIteratorHasNextRequestContext.h"
#include "DirectoryIteratorNextRequestContext.h"
#include "DirectoryIteratorDestroyRequestContext.h"
#include "PathExistRequestContext.h"
#include "IsFileRequestContext.h"
#include "ActualModificationIdRequestContext.h"
#include "FileSizeRequestContext.h"

namespace sirius::drive::contract {

StorageServer::StorageServer( std::weak_ptr<ModificationsExecutor> executor )
        : m_executor( std::move( executor )) {}

void StorageServer::registerService( grpc::ServerBuilder& builder ) {
    builder.RegisterService( &m_service );
    m_cq = builder.AddCompletionQueue();
}

void StorageServer::run( std::weak_ptr<IOContextProvider> contextKeeper ) {
    m_context = std::move( contextKeeper );
    m_serviceIsActive = std::make_shared<bool>( true );

    registerSynchronizeStorage();
    registerInitiateModifications();
    registerInitiateSandboxModifications();
    registerApplySandboxStorageModifications();
    registerEvaluateStorageHash();
    registerApplyStorageModifications();
    registerOpenFile();
    registerReadFile();
    registerWriteFile();
    registerCloseFile();
    registerFlush();
    registerGetFileInfo();
    registerGetActualModificationId();
    registerGetFilesystem();
    registerDirectoryIteratorCreate();
    registerDirectoryIteratorHasNext();
    registerDirectoryIteratorNext();
    registerDirectoryIteratorDestroy();
    registerRemoveFilesystemEntry();
    registerMoveFilesystemEntry();
    registerCreateDirectories();
    registerPathExist();
    registerFileSize();
    registerIsFile();

    m_thread = std::thread( [this] {
        waitForQueries();
    } );
}

StorageServer::~StorageServer() {
    std::cout << "~contract server";
    *m_serviceIsActive = false;
    m_cq->Shutdown();
    if ( m_thread.joinable()) {
        m_thread.join();
    }
}

void StorageServer::waitForQueries() {
    void* pTag;
    bool ok;
    while ( m_cq->Next( &pTag, &ok )) {
        auto* pQuery = static_cast<RPCTag*>(pTag);
        pQuery->process( ok );
        delete pQuery;
    }
}

void StorageServer::registerSynchronizeStorage() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto context = std::make_shared<SynchronizeStorageRequestContext>( m_service, *m_cq, m_serviceIsActive,
                                                                       m_executor );
    auto* tag = new AcceptRequestRPCTag( context, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }

        registerSynchronizeStorage();
    }, m_context );
    context->run( tag );
}

void StorageServer::registerInitiateModifications() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto synchronizeContext = std::make_shared<InitiateModificationsRequestContext>( m_service, *m_cq,
                                                                                     m_serviceIsActive, m_executor );
    auto* tag = new AcceptRequestRPCTag( synchronizeContext, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerInitiateModifications();
    }, m_context );
    synchronizeContext->run( tag );
}

void StorageServer::registerInitiateSandboxModifications() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto synchronizeContext = std::make_shared<InitiateSandboxModificationsRequestContext>( m_service, *m_cq,
                                                                                            m_serviceIsActive,
                                                                                            m_executor );
    auto* tag = new AcceptRequestRPCTag( synchronizeContext, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerInitiateSandboxModifications();
    }, m_context );
    synchronizeContext->run( tag );
}

void StorageServer::registerApplySandboxStorageModifications() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto synchronizeContext = std::make_shared<ApplySandboxModificationsRequestContext>( m_service, *m_cq,
                                                                                         m_serviceIsActive,
                                                                                         m_executor );
    auto* tag = new AcceptRequestRPCTag( synchronizeContext, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerApplySandboxStorageModifications();
    }, m_context );
    synchronizeContext->run( tag );
}

void StorageServer::registerEvaluateStorageHash() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto synchronizeContext = std::make_shared<EvaluateStorageHashRequestContext>( m_service, *m_cq, m_serviceIsActive,
                                                                                   m_executor );
    auto* tag = new AcceptRequestRPCTag( synchronizeContext, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerEvaluateStorageHash();
    }, m_context );
    synchronizeContext->run( tag );
}

void StorageServer::registerApplyStorageModifications() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto synchronizeContext = std::make_shared<ApplyStorageModificationsRequestContext>( m_service, *m_cq,
                                                                                         m_serviceIsActive,
                                                                                         m_executor );
    auto* tag = new AcceptRequestRPCTag( synchronizeContext, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerApplyStorageModifications();
    }, m_context );
    synchronizeContext->run( tag );
}

void StorageServer::registerOpenFile() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto synchronizeContext = std::make_shared<OpenFileRequestContext>( m_service, *m_cq, m_serviceIsActive,
                                                                        m_executor );
    auto* tag = new AcceptRequestRPCTag( synchronizeContext, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerOpenFile();
    }, m_context );
    synchronizeContext->run( tag );
}

void StorageServer::registerReadFile() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto synchronizeContext = std::make_shared<ReadFileRequestContext>( m_service, *m_cq, m_serviceIsActive,
                                                                        m_executor );
    auto* tag = new AcceptRequestRPCTag( synchronizeContext, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerReadFile();
    }, m_context );
    synchronizeContext->run( tag );
}

void StorageServer::registerWriteFile() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto synchronizeContext = std::make_shared<WriteFileRequestContext>( m_service, *m_cq, m_serviceIsActive,
                                                                         m_executor );
    auto* tag = new AcceptRequestRPCTag( synchronizeContext, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerWriteFile();
    }, m_context );
    synchronizeContext->run( tag );
}

void StorageServer::registerCloseFile() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto synchronizeContext = std::make_shared<CloseFileRequestContext>( m_service, *m_cq, m_serviceIsActive,
                                                                         m_executor );
    auto* tag = new AcceptRequestRPCTag( synchronizeContext, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerCloseFile();
    }, m_context );
    synchronizeContext->run( tag );
}

void StorageServer::registerFlush() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto synchronizeContext = std::make_shared<FlushRequestContext>( m_service, *m_cq, m_serviceIsActive, m_executor );
    auto* tag = new AcceptRequestRPCTag( synchronizeContext, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerFlush();
    }, m_context );
    synchronizeContext->run( tag );
}

void StorageServer::registerGetFileInfo() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto context = std::make_shared<FileInfoRequestContext>( m_service, *m_cq, m_serviceIsActive, m_executor );
    auto* tag = new AcceptRequestRPCTag( context, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerGetFileInfo();
    }, m_context );
    context->run( tag );
}

void StorageServer::registerGetActualModificationId()
{
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto context = std::make_shared<ActualModificationIdRequestContext>( m_service, *m_cq, m_serviceIsActive, m_executor );
    auto* tag = new AcceptRequestRPCTag( context, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerGetActualModificationId();
        }, m_context );
    context->run( tag );
}

void StorageServer::registerGetFilesystem() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto context = std::make_shared<FilesystemRequestContext>( m_service, *m_cq, m_serviceIsActive, m_executor );
    auto* tag = new AcceptRequestRPCTag( context, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerGetFilesystem();
    }, m_context );
    context->run( tag );
}

void StorageServer::registerDirectoryIteratorCreate() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto context = std::make_shared<DirectoryIteratorCreateRequestContext>( m_service, *m_cq, m_serviceIsActive,
                                                                            m_executor );
    auto* tag = new AcceptRequestRPCTag( context, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerDirectoryIteratorCreate();
    }, m_context );
    context->run( tag );
}

void StorageServer::registerDirectoryIteratorHasNext() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto context = std::make_shared<DirectoryIteratorHasNextRequestContext>( m_service, *m_cq, m_serviceIsActive,
                                                                             m_executor );
    auto* tag = new AcceptRequestRPCTag( context, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerDirectoryIteratorHasNext();
    }, m_context );
    context->run( tag );
}

void StorageServer::registerDirectoryIteratorNext() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto context = std::make_shared<DirectoryIteratorNextRequestContext>( m_service, *m_cq, m_serviceIsActive,
                                                                          m_executor );
    auto* tag = new AcceptRequestRPCTag( context, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerDirectoryIteratorNext();
    }, m_context );
    context->run( tag );
}

void StorageServer::registerDirectoryIteratorDestroy() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto context = std::make_shared<DirectoryIteratorDestroyRequestContext>( m_service, *m_cq, m_serviceIsActive,
                                                                             m_executor );
    auto* tag = new AcceptRequestRPCTag( context, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerDirectoryIteratorDestroy();
    }, m_context );
    context->run( tag );
}

void StorageServer::registerRemoveFilesystemEntry() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto context = std::make_shared<RemoveFilesystemEntryRequestContext>( m_service, *m_cq, m_serviceIsActive,
                                                                          m_executor );
    auto* tag = new AcceptRequestRPCTag( context, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerRemoveFilesystemEntry();
    }, m_context );
    context->run( tag );
}

void StorageServer::registerMoveFilesystemEntry() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto context = std::make_shared<MoveFilesystemEntryRequestContext>( m_service, *m_cq, m_serviceIsActive,
                                                                        m_executor );
    auto* tag = new AcceptRequestRPCTag( context, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerMoveFilesystemEntry();
    }, m_context );
    context->run( tag );
}

void StorageServer::registerCreateDirectories() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto context = std::make_shared<CreateDirectoriesRequestContext>( m_service, *m_cq, m_serviceIsActive, m_executor );
    auto* tag = new AcceptRequestRPCTag( context, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerCreateDirectories();
    }, m_context );
    context->run( tag );
}

void StorageServer::registerPathExist() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto context = std::make_shared<PathExistRequestContext>( m_service, *m_cq, m_serviceIsActive, m_executor );
    auto* tag = new AcceptRequestRPCTag( context, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerPathExist();
    }, m_context );
    context->run( tag );
}

void StorageServer::registerFileSize() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto context = std::make_shared<FileSizeRequestContext>( m_service, *m_cq, m_serviceIsActive, m_executor );
    auto* tag = new AcceptRequestRPCTag( context, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerFileSize();
        }, m_context );
    context->run( tag );
}

void StorageServer::registerIsFile() {
    if ( !*m_serviceIsActive ) {
        return;
    }

    auto context = std::make_shared<IsFileRequestContext>( m_service, *m_cq, m_serviceIsActive, m_executor );
    auto* tag = new AcceptRequestRPCTag( context, [this, serviceIsActive = m_serviceIsActive] {
        if ( !*serviceIsActive ) {
            return;
        }
        registerIsFile();
    }, m_context );
    context->run( tag );
}

}
