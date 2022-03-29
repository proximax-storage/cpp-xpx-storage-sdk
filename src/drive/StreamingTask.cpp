/*
*** Copyright 2022 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/Streaming.h"
#include "DriveTaskBase.h"
#include "drive/FsTree.h"
#include "drive/FlatDrive.h"
#include "DriveParams.h"
#include "UpdateDriveTaskBase.h"

namespace sirius::drive
{

namespace fs = std::filesystem;

class StreamingTask : public UpdateDriveTaskBase
{
    std::unique_ptr<StreamRequest> m_request;

public:
    
    StreamingTask(  mobj<StreamRequest>&& request,
                    DriveParams& drive,
                    ModifyOpinionController& opinionTaskController
                  )
            : UpdateDriveTaskBase( DriveTaskType::STREAMING_REQUEST, drive, opinionTaskController )
            , m_request( std::move(request) )
    {
        _ASSERT( m_request )
    }
    
    ~StreamingTask() = default;
    
    const Hash256& getModificationTransactionHash() override
    {
        //(???)
        return m_request->m_streamId;
    }

    void continueSynchronizingDriveWithSandbox() override
    {
        //TODO
    }

    uint64_t getToBeApprovedDownloadSize() override
    {
        //TODO
        return 0;
    }

    void myOpinionIsCreated() override
    {
        //TODO
    }


    // Whether the finishTask can be called by the task itself
    void tryBreakTask() override
    {
        //TODO
    }

    void run() override
    {
        //TODO
    }

    void terminate() override
    {
        //TODO
    }

};


std::unique_ptr<StreamingTask> createStreamingTask( mobj<StreamRequest>&&       request,
                                                    DriveParams&                drive,
                                                    ModifyOpinionController&    opinionTaskController )
{
    return std::make_unique<StreamingTask>( std::move(request), drive, opinionTaskController );
}

} // namespace sirius::drive


