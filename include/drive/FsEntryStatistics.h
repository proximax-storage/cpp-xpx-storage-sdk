/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <cinttypes>
#include <memory>

namespace sirius::drive
{

class FsEntryStatistics
{

public:

    uint64_t m_blocks = 0;

public:

    virtual ~FsEntryStatistics() = default;

    virtual uint64_t totalBlocks() const
    {
        return m_blocks;
    }
};

class FolderStatistics
        : public FsEntryStatistics
{

public:

    uint64_t m_childrenBlocks = 0;

public:

    uint64_t totalBlocks() const override
    {
        return m_blocks + m_childrenBlocks;
    }

    FolderStatistics operator-( const FsEntryStatistics& entryStatistics ) const
    {
        FolderStatistics folderStatistics = *this;
        folderStatistics.m_childrenBlocks -= entryStatistics.totalBlocks();
        return folderStatistics;
    }

    FolderStatistics operator+( const FsEntryStatistics& entryStatistics ) const
    {
        FolderStatistics folderStatistics = *this;
        folderStatistics.m_childrenBlocks += entryStatistics.totalBlocks();
        return folderStatistics;
    }

    FolderStatistics& operator-=( const FsEntryStatistics& entryStatistics )
    {
        m_childrenBlocks -= entryStatistics.totalBlocks();
        return *this;
    }

    FolderStatistics& operator+=( const FsEntryStatistics& entryStatistics )
    {
        m_childrenBlocks += entryStatistics.totalBlocks();
        return *this;
    }
};

class FileStatistics
        : public FsEntryStatistics
{

public:

    uint64_t totalBlocks() const override {
        return m_blocks;
    }

};

class FolderStatisticsNode
{

private:

    std::weak_ptr<FolderStatisticsNode> m_parent;
    FolderStatistics m_statistics;

public:

    void update( const FsEntryStatistics& oldChild, const FsEntryStatistics& newChild )
    {
        FolderStatistics oldStatistics = m_statistics;
        m_statistics -= oldChild;
        m_statistics += newChild;
        auto parent = m_parent.lock();
        if ( !parent )
        {
            return;
        }
        parent->update( oldStatistics, m_statistics );
    }

    void setParent( std::weak_ptr<FolderStatisticsNode> parent )
    {
        FolderStatistics zeroStatistics;
        auto oldParent = m_parent.lock();
        if ( oldParent )
        {
            oldParent->update( m_statistics, zeroStatistics );
        }

        m_parent = std::move( parent );

        auto newParent = m_parent.lock();

        if ( newParent )
        {
            newParent->update( zeroStatistics, m_statistics );
        }
    }

    void addBlock()
    {
        FolderStatistics oldStatistics = m_statistics;
        m_statistics.m_blocks += 1;
        auto parent = m_parent.lock();
        if ( !parent )
        {
            return;
        }
        parent->update( oldStatistics, m_statistics );
    }

    void removeBlock()
    {
        FolderStatistics oldStatistics = m_statistics;
        m_statistics.m_blocks -= 1;
        auto parent = m_parent.lock();
        if ( !parent )
        {
            return;
        }
        parent->update( oldStatistics, m_statistics );
    }

    const FolderStatistics& statistics() const
    {
        return m_statistics;
    }

};

class FileStatisticsNode
{

private:

    std::weak_ptr<FolderStatisticsNode> m_parent;
    FileStatistics m_statistics;

public:

    void setParent( std::weak_ptr<FolderStatisticsNode> parent )
    {
        FileStatistics zeroStatistics;
        auto oldParent = m_parent.lock();
        if ( oldParent )
        {
            oldParent->update( m_statistics, zeroStatistics );
        }

        m_parent = std::move( parent );

        auto newParent = m_parent.lock();

        if ( newParent )
        {
            newParent->update( zeroStatistics, m_statistics );
        }
    }

    void addBlock()
    {
        FileStatistics oldStatistics = m_statistics;
        m_statistics.m_blocks += 1;
        auto parent = m_parent.lock();
        if ( !parent )
        {
            return;
        }
        parent->update( oldStatistics, m_statistics );
    }

    void removeBlock()
    {
        FileStatistics oldStatistics = m_statistics;
        m_statistics.m_blocks -= 1;
        auto parent = m_parent.lock();
        if ( !parent )
        {
            return;
        }
        parent->update( oldStatistics, m_statistics );
    }

    const FileStatistics& statistics() const
    {
        return m_statistics;
    }

};

}