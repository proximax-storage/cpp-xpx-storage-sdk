/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#include "drive/FsEntryStatistics.h"

namespace sirius::drive
{

FolderStatistics FolderStatistics::operator-( const FsEntryStatistics& entryStatistics ) const
{
    FolderStatistics folderStatistics = *this;
    folderStatistics.m_childrenBlocks -= entryStatistics.totalBlocks();
    return folderStatistics;
}

FolderStatistics FolderStatistics::operator+( const FsEntryStatistics& entryStatistics ) const
{
    FolderStatistics folderStatistics = *this;
    folderStatistics.m_childrenBlocks += entryStatistics.totalBlocks();
    return folderStatistics;
}

FolderStatistics FolderStatistics::operator-=( const FsEntryStatistics& entryStatistics )
{
    m_childrenBlocks -= entryStatistics.totalBlocks();
    return *this;
}

FolderStatistics FolderStatistics::operator+=( const FsEntryStatistics& entryStatistics )
{
    m_childrenBlocks += entryStatistics.totalBlocks();
    return *this;
}

uint64_t FolderStatistics::totalBlocks() const
{
    return m_blocks + m_childrenBlocks;
}

void FolderStatisticsNode::update( const FsEntryStatistics& oldChild, const FsEntryStatistics& newChild )
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

void FolderStatisticsNode::setParent( std::weak_ptr<FolderStatisticsNode> parent )
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

void FolderStatisticsNode::addBlock()
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

void FolderStatisticsNode::removeBlock()
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

const FolderStatistics& FolderStatisticsNode::statistics() const
{
    return m_statistics;
}

uint64_t FileStatistics::totalBlocks() const
{
    return m_blocks;
}

void FileStatisticsNode::setParent( std::weak_ptr<FolderStatisticsNode> parent )
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

void FileStatisticsNode::addBlock()
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

void FileStatisticsNode::removeBlock()
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

}
