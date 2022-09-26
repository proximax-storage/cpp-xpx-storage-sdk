/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "DriveTaskBase.h"
#include "ManualModificationsRequests.h"
#include "drive/log.h"

namespace sirius::drive {

namespace fs = std::filesystem;

class ManualModificationsTask
        : public DriveTaskBase {

    std::unique_ptr<FsTree> m_lowerSandboxFsTree;
    std::unique_ptr<FsTree> m_upperSandboxFsTree;

    std::map<uint64_t, std::fstream> m_openFilesWrite;
    std::map<uint64_t, std::fstream> m_openFilesRead;

    uint64_t m_totalFilesOpened = 0;

    ManualModificationsTask(const DriveTaskType& type,
                            DriveParams& drive) : DriveTaskBase(type, drive) {}

public:
    void run() override {
        DBG_MAIN_THREAD

        _ASSERT(m_drive.m_fsTree)
        m_lowerSandboxFsTree = std::make_unique<FsTree>(*m_drive.m_fsTree);
    }

    void initiateSandboxModifications() {
        DBG_MAIN_THREAD

        m_upperSandboxFsTree = std::make_unique<FsTree>(*m_drive.m_fsTree);
    }

    void openFile(OpenFileRequest&& request) {
        DBG_MAIN_THREAD

//        uint64_t fileId = m_totalFilesOpened;
//        m_totalFilesOpened++;
//
//        if (request.m_mode == OpenFileMode::READ) {
//            _ASSERT(m_upperSandboxFsTree);
//
////            m_drive.executeOnBackgroundThread([fileId] {
////            });
//        }
//        else {
//
//        }
    }

    void createStream(std::string&& path, OpenFileMode mode, uint64_t fileId) {
        DBG_BG_THREAD

        std::ios_base::openmode m = std::ios_base::binary;

        if (mode == OpenFileMode::READ) {
            m |= std::ios_base::in;
        }
        else {
            m |= std::ios_base::out;
        }

        // TODO We use shared pointer here because can not pass move-only objects below
        auto stream = std::make_shared<std::fstream>(path, m);
        m_drive.executeOnSessionThread([=, this] {
            onFileOpened(std::move(*stream), mode, fileId);
        });
    }

    void onFileOpened(std::fstream&& stream, OpenFileMode mode, uint64_t m_fileId) {

        DBG_MAIN_THREAD

        _ASSERT(!m_openFilesRead.contains(m_fileId))
        _ASSERT(!m_openFilesWrite.contains(m_fileId))

        if (mode == OpenFileMode::READ) {
            m_openFilesRead[m_fileId] = std::move(stream);
        }
        else {
            m_openFilesRead[m_fileId] = std::move(stream);
        }
    }

};
}