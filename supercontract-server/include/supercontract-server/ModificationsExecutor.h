/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

namespace sirius::drive::contract
{

class ModificationsExecutor
{

public:

    virtual ~ModificationsExecutor() = default;

    virtual void
    initiateManualModifications( const DriveKey& driveKey, const InitiateModificationsRequest& request ) = 0;

    virtual void initiateManualSandboxModifications( const DriveKey& driveKey,
                                                     const InitiateSandboxModificationsRequest& request ) = 0;

    virtual void openFile( const DriveKey& driveKey, const OpenFileRequest& request ) = 0;

    virtual void writeFile( const DriveKey& driveKey, const WriteFileRequest& request ) = 0;

    virtual void readFile( const DriveKey& driveKey, const ReadFileRequest& request ) = 0;

    virtual void flush( const DriveKey& driveKey, const FlushRequest& request ) = 0;

    virtual void closeFile( const DriveKey& driveKey, const CloseFileRequest& request ) = 0;

    virtual void
    applySandboxManualModifications( const DriveKey& driveKey, const ApplySandboxModificationsRequest& request ) = 0;

    virtual void evaluateStorageHash( const DriveKey& driveKey, const EvaluateStorageHashRequest& request ) = 0;

    virtual void
    applyStorageManualModifications( const DriveKey& driveKey, const ApplyStorageModificationsRequest& request ) = 0;

    virtual void manualSynchronize( const DriveKey& driveKey, const SynchronizationRequest& request ) = 0;

};

}
