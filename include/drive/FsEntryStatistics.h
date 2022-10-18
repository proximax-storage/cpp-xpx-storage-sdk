/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <cinttypes>
#include <memory>

namespace sirius::drive {

class FsEntryStatistics {

public:

    uint64_t m_blocks = 0;

public:

    virtual uint64_t totalBlocks() const = 0;
};

class FolderStatistics: public FsEntryStatistics {

public:

    uint64_t m_childrenBlocks = 0;

public:

    uint64_t totalBlocks() const override;

    FolderStatistics operator -(const FsEntryStatistics& entryStatistics) const;
    FolderStatistics operator +(const FsEntryStatistics& entryStatistics) const;
    FolderStatistics operator -=(const FsEntryStatistics& entryStatistics);
    FolderStatistics operator +=(const FsEntryStatistics& entryStatistics);
};

class FileStatistics: public FsEntryStatistics {

public:

    uint64_t totalBlocks() const override;

};

class FolderStatisticsNode {

private:

    std::weak_ptr<FolderStatisticsNode> m_parent;
    FolderStatistics m_statistics;

public:

    void update(const FsEntryStatistics& oldChild, const FsEntryStatistics& newChild);

    void setParent( std::weak_ptr<FolderStatisticsNode> );

    void addBlock();

    void removeBlock();

    const FolderStatistics& statistics() const;

};

class FileStatisticsNode {

private:

    std::weak_ptr<FolderStatisticsNode> m_parent;
    FileStatistics m_statistics;

public:

    void setParent( std::weak_ptr<FolderStatisticsNode> );

    void addBlock();

    void removeBlock();

    const FileStatistics& statistics() const;

};

}