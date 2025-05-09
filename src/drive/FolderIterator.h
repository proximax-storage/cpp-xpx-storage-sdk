/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <stack>
#include "drive/FsTree.h"

struct IteratorValue {
    std::string m_name;
    uint32_t m_depth;
};

namespace sirius::drive
{
struct FolderIterator
{

private:

    struct StackEntry
    {
        const std::map<std::string, Folder::Child>& m_children;
        decltype( m_children.begin()) m_it;

        explicit StackEntry( const std::map<std::string, Folder::Child>& children )
                : m_children( children )
                , m_it( m_children.begin())
        {}
    };

    std::stack<StackEntry> m_stack;
    std::shared_ptr<FolderStatisticsNode> m_statisticsNode;
    bool m_recursive;

public:

    explicit FolderIterator( const Folder& folder, bool recursive );

    FolderIterator( const FolderIterator& ) = delete;
    FolderIterator( FolderIterator&& ) = default;

    ~FolderIterator();

    bool hasNext();

    std::optional<IteratorValue> next();

private:

    void clearStack();
};
}