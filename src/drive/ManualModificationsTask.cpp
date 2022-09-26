/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "DriveTaskBase.h"
#include "ManualModificationsRequests.h"
#include "drive/log.h"
#include "drive/Utils.h"

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

        _ASSERT(m_upperSandboxFsTree);

        uint64_t fileId = m_totalFilesOpened;
        m_totalFilesOpened++;

        fs::path p(request.m_path);

        auto pFolder = m_lowerSandboxFsTree->getFolderPtr(p.parent_path());

        if (!pFolder) {
            // TODO return unsuccessful result
            return;
        }

        if (request.m_mode == OpenFileMode::READ) {

            auto it = pFolder->childs().find(p.filename());

            if (it == pFolder->childs().end()) {
                // TODO return unsuccessful result
                return;
            }

            const auto& child = it->second;

            if (!isFile(child)) {
                // TODO return unsuccessful result
                return;
            }

            auto name = toString(getFile(child).hash());

            auto absolutePath = m_drive.m_driveFolder / name;

            m_drive.executeOnBackgroundThread([=, this, mode=request.m_mode] () mutable {
                createStream(std::move(absolutePath), mode, fileId);
            });
        }
        else {
            if (!pFolder->childs().contains(p.filename())) {
                m_lowerSandboxFsTree->addModifiableFile(p.parent_path(), p.filename());
            }

            auto it = pFolder->childs().find(p.filename());

            _ASSERT(it != pFolder->childs().end());

            const auto& child = it->second;

            if (!isFile(child)) {
                // TODO return unsuccessful result
                return;
            }

            auto name = toString(getFile(child).hash());

            auto absolutePath = m_drive.m_driveFolder / name;

            m_drive.executeOnBackgroundThread([=, this, mode=request.m_mode] () mutable {
                createStream(std::move(absolutePath), mode, fileId);
            });
        }
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

        _ASSERT(stream.is_open())

        if (mode == OpenFileMode::READ) {
            m_openFilesRead[m_fileId] = std::move(stream);
        }
        else {
            m_openFilesWrite[m_fileId] = std::move(stream);
        }
    }

    void readFile(ReadFileRequest&& request) {

        DBG_MAIN_THREAD

        auto it = m_openFilesRead.find(request.m_fileId);

        if (it == m_openFilesRead.end()) {
            // TODO return unsuccessful result
            return;
        }

    }

    void readStream(std::fstream& stream, uint64_t bytes) {
        DBG_BG_THREAD

        std::vector<uint8_t> buffer(bytes, 0);
        stream.read(reinterpret_cast<char *>(buffer.data()), buffer.size());
        auto read = stream.gcount();
        buffer.resize(read);
    }

    void writeStream(std::fstream& stream, std::vector<uint8_t>&& buffer) {
        DBG_BG_THREAD

        stream.write(reinterpret_cast<char *>(buffer.data()), buffer.size());
    }

    void closeStream(std::fstream& stream) {
        stream.close();
    }

};
}