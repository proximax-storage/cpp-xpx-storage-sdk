/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "FolderIterator.h"

namespace sirius::drive
{

FolderIterator::FolderIterator( const Folder& folder, bool recursive )
        : m_statisticsNode( folder.statisticsNode())
        , m_recursive( recursive )
{
    if ( !folder.childs().empty())
    {
        m_stack.push( StackEntry( folder.childs()));
    }
    m_statisticsNode->addBlock();
}

FolderIterator::~FolderIterator()
{
    if ( m_statisticsNode )
    {
        m_statisticsNode->removeBlock();
    }
}

bool FolderIterator::hasNext()
{
    return !m_stack.empty();
}

std::optional<IteratorValue> FolderIterator::next()
{
    if ( !hasNext())
    {
        return {};
    }

    IteratorValue iteratorValue;
    iteratorValue.m_depth = m_stack.size() - 1;

    auto& entry = m_stack.top();
    if ( isFile( entry.m_it->second ))
    {
        iteratorValue.m_name = getFile( entry.m_it->second ).name();
    } else
    {
        auto& folder = getFolder( entry.m_it->second );
        iteratorValue.m_name = folder.name();
        if ( m_recursive && !folder.childs().empty() )
        {
            m_stack.push( StackEntry( folder.childs()));
        }
    }
    entry.m_it++;
    clearStack();
    return iteratorValue;
}

void FolderIterator::clearStack()
{
    while ( !m_stack.empty() && m_stack.top().m_it == m_stack.top().m_children.end())
    {
        m_stack.pop();
    }
}
}