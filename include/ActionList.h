/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include "types.h"

#include <string>
#include <vector>
//#include <fstream>


namespace xpx_storage_sdk {

    // action_list_id::code
    namespace action_list_id {
        enum code
        {
            none        = 0,
            upload      = 1,
            new_folder  = 2,
            rename      = 3,
            remove      = 4,
            blob_hash   = 5
        };
    };

    // Action
    struct Action {
        Action() = default;

        Action( action_list_id::code code, std::string p1, std::string p2 = "" ) : m_actionId(code), m_param1(p1), m_param2(p2) {}
        Action( action_list_id::code code, FileHash hash ) : m_actionId(code), m_hash(hash) {}

        static Action upload( std::string pathToLocalFile, std::string remoteFileNameWithPath ) {
            return Action( action_list_id::upload, pathToLocalFile, remoteFileNameWithPath );
        }

        static Action newFolder( std::string remoteFolderNameWithPath ) {
            return Action( action_list_id::new_folder, remoteFolderNameWithPath );
        }

        static Action rename( std::string oldNameWithPath, std::string newNameWithPath ) {
            return Action( action_list_id::rename, oldNameWithPath, newNameWithPath );
        }

        static Action remove( std::string remoteObjectNameWithPath ) {
            return Action( action_list_id::remove, remoteObjectNameWithPath );
        }

        template <class Archive> void serialize( Archive & arch ) {

            arch( m_actionId );

            if ( m_actionId == action_list_id::blob_hash ) {
                arch( m_hash );
            }
            else {
                arch( m_param1 );

                if ( m_actionId == action_list_id::rename ) {
                    arch( m_param2 );
                }

                if ( m_actionId == action_list_id::upload ) {
                    arch( m_param2 );
                    arch( m_hash );
                }
            }
        }

        bool operator==( const Action& a ) const { return m_actionId==a.m_actionId && m_param1==a.m_param1 && m_param2 == a.m_param2; }

        action_list_id::code m_actionId = action_list_id::none;
        std::string          m_param1;
        std::string          m_param2;
        FileHash             m_hash;
    };

    // ActionList
    struct ActionList : public std::vector<Action>
    {
        void serialize( std::string fileName ) const;
        void deserialize( std::string fileName );
    };

};
