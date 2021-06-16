/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"
#include "plugins.h"
#include <libtorrent/torrent_handle.hpp>

namespace sirius { namespace drive {

    using lt_handle  = lt::torrent_handle;

    // action_list_id::code
    namespace action_list_id {
        enum code
        {
            upload      = 1,
            new_folder  = 2,
            move        = 3,
            remove      = 4,
        };
    };

    struct EmptyStruct {};

    // Action
    struct PLUGIN_API Action {
        Action() = default;

        static Action upload( const std::string& pathToLocalFile, const std::string& remoteFileNameWithPath ) {
            return Action( action_list_id::upload, pathToLocalFile, remoteFileNameWithPath );
        }

        static Action newFolder( const std::string& remoteFolderNameWithPath ) {
            return Action( action_list_id::new_folder, remoteFolderNameWithPath );
        }

        static Action move( const std::string& oldNameWithPath, const std::string& newNameWithPath ) {
            return Action( action_list_id::move, oldNameWithPath, newNameWithPath );
        }

        static Action remove( const std::string& remoteObjectNameWithPath ) {
            return Action( action_list_id::remove, remoteObjectNameWithPath );
        }

        std::string getSource() const
        {
            assert( m_actionId == action_list_id::upload || m_actionId == action_list_id::move );
            return m_param1;
        }

        std::string getDestination() const
        {
            assert( m_actionId == action_list_id::upload || m_actionId == action_list_id::move );
            return m_param2;
        }

        std::string getNewFolder() const
        {
            assert( m_actionId == action_list_id::new_folder );
            return m_param1;
        }

        std::string getRemovedItem() const
        {
            assert( m_actionId == action_list_id::remove );
            return m_param1;
        }

        template <class Archive> void serialize( Archive & arch ) {

            arch( m_actionId );

            arch( m_param1 );

            if ( m_actionId == action_list_id::upload || m_actionId == action_list_id::move ) {
                arch( m_param2 );
            }
        }

        bool operator==( const Action& a ) const { return m_actionId==a.m_actionId && m_param1==a.m_param1 && m_param2 == a.m_param2; }

        action_list_id::code m_actionId;
        std::string          m_param1;
        std::string          m_param2;

    private:
        Action( action_list_id::code code, const std::string& p1, const std::string& p2 = "" )
            : m_actionId(code), m_param1(p1), m_param2(p2)
        {
            if ( m_actionId != action_list_id::upload ) {
                while ( !m_param1.empty() && m_param1[0] == '/')
                    m_param1 = m_param1.substr( 1 );
            }

            while ( !m_param2.empty() && m_param2[0] == '/') {
                m_param2 = m_param2.substr( 1 );
            }

            if ( m_actionId != action_list_id::move ) {
                while( m_param2.back() == '/')
                    m_param2.resize(m_param2.size()-1 );
            }
        }

    private:
        friend class DefaultDrive;
        friend class DefaultFlatDrive;

        mutable bool         m_isInvalid = false;
        lt_handle            m_ltHandle;
    };

    // ActionList
    struct PLUGIN_API ActionList : public std::vector<Action>
    {
        void serialize( std::string fileName ) const;
        void deserialize( std::string fileName );

        void dbgPrint() const;
    };

}}
