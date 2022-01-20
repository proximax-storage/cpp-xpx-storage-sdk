/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <filesystem>

#include "drive/Utils.h"
#include "drive/Session.h"

namespace sirius::drive {

namespace fs = std::filesystem;

class FlatDrivePaths {
protected:
    FlatDrivePaths( const std::string&  replicatorRootFolder,
                    const std::string&      replicatorSandboxRootFolder,
                    const Key&              drivePubKey )
                    :
                    m_drivePubKey( drivePubKey ),
                    m_replicatorRoot( replicatorRootFolder ),
                    m_replicatorSandboxRoot( replicatorSandboxRootFolder )
                    {}

                    virtual~FlatDrivePaths() {}

protected:
    const Key       m_drivePubKey;

    const fs::path  m_replicatorRoot;
    const fs::path  m_replicatorSandboxRoot;

public:
    // Drive paths
    const fs::path  m_driveRootPath     = m_replicatorRoot / arrayToString(m_drivePubKey.array());
    const fs::path  m_driveFolder       = m_driveRootPath  / "drive";
    const fs::path  m_torrentFolder     = m_driveRootPath  / "torrent";
    const fs::path  m_fsTreeFile        = m_driveRootPath  / "fs_tree" / FS_TREE_FILE_NAME;
    const fs::path  m_fsTreeTorrent     = m_driveRootPath  / "fs_tree" / FS_TREE_FILE_NAME ".torrent";

    // Sandbox paths
    const fs::path  m_sandboxRootPath       = m_replicatorSandboxRoot / arrayToString(m_drivePubKey.array());
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

}