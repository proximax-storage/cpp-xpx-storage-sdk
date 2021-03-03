/*
*** Copyright 2019 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once

#include <string>
#include <vector>
#include <fstream>
//#include <cereal/types/vector.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/archives/binary.hpp>


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
        };
    };

    // Action
    struct Action {
        Action() = default;

        Action( action_list_id::code code, std::string p1, std::string p2 = "" ) : m_actionId(code), m_param1(p1), m_param2(p2) {}

        static Action uplaod( std::string pathToLocalFile, std::string remoteFileNameWithPath ) {
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

        template <class Archive>
        void serialize( Archive & ar )
        {
            ar( m_actionId, m_param1, m_param2 );
        }

        bool operator==( const Action& a ) const { return m_actionId==a.m_actionId && m_param1==a.m_param1 && m_param2 == a.m_param2; }

        action_list_id::code m_actionId = action_list_id::none;
        std::string          m_param1;
        std::string          m_param2;
    };

    // ActionList
    struct ActionList : public std::vector<Action>
    {
        void serialize( std::string fileName )
        {
            std::ofstream os( fileName, std::ios::binary );
            cereal::BinaryOutputArchive archive( os );
            archive( static_cast<uint16_t>( size() ));
            for( uint i=0; i<size(); i++ ) {
                archive( at(i) );
            }
        }

        void deserialize( std::string fileName )
        {
            std::ifstream is( fileName, std::ios::binary );
            cereal::BinaryInputArchive iarchive(is);

            uint16_t size;
            iarchive( size );
            for( uint i=0; i<size; i++ ) {
                push_back( Action() );
                iarchive( at(i) );
            }
        }
    };

};
