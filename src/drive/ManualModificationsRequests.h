/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once

#include <vector>


namespace sirius::drive {

enum class OpenFileMode {
    READ, WRITE
};

struct OpenFileRequest {
    OpenFileMode m_mode;
    std::string  m_path;
};

struct ReadFileRequest {
    uint64_t m_fileId;
    uint64_t m_bytes;
};

struct WriteFileRequest {
    uint64_t m_fileId;
    std::vector<uint8_t> m_buffer;
};

}