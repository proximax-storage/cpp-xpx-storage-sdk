/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "Drive.h"
#include "FileTransmitter.h"
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

namespace xpx_storage_sdk {
using namespace fs_tree;

char byteMap[256][2] = {
    {'0','0'}, {'0','1'}, {'0','2'}, {'0','3'}, {'0','4'}, {'0','5'}, {'0','6'}, {'0','7'}, {'0','8'}, {'0','9'}, {'0','a'}, {'0','b'}, {'0','c'}, {'0','d'}, {'0','e'}, {'0','f'},
    {'1','0'}, {'1','1'}, {'1','2'}, {'1','3'}, {'1','4'}, {'1','5'}, {'1','6'}, {'1','7'}, {'1','8'}, {'1','9'}, {'1','a'}, {'1','b'}, {'1','c'}, {'1','d'}, {'1','e'}, {'1','f'},
    {'2','0'}, {'2','1'}, {'2','2'}, {'2','3'}, {'2','4'}, {'2','5'}, {'2','6'}, {'2','7'}, {'2','8'}, {'2','9'}, {'2','a'}, {'2','b'}, {'2','c'}, {'2','d'}, {'2','e'}, {'2','f'},
    {'3','0'}, {'3','1'}, {'3','2'}, {'3','3'}, {'3','4'}, {'3','5'}, {'3','6'}, {'3','7'}, {'3','8'}, {'3','9'}, {'3','a'}, {'3','b'}, {'3','c'}, {'3','d'}, {'3','e'}, {'3','f'},
    {'4','0'}, {'4','1'}, {'4','2'}, {'4','3'}, {'4','4'}, {'4','5'}, {'4','6'}, {'4','7'}, {'4','8'}, {'4','9'}, {'4','a'}, {'4','b'}, {'4','c'}, {'4','d'}, {'4','e'}, {'4','f'},
    {'5','0'}, {'5','1'}, {'5','2'}, {'5','3'}, {'5','4'}, {'5','5'}, {'5','6'}, {'5','7'}, {'5','8'}, {'5','9'}, {'5','a'}, {'5','b'}, {'5','c'}, {'5','d'}, {'5','e'}, {'5','f'},
    {'6','0'}, {'6','1'}, {'6','2'}, {'6','3'}, {'6','4'}, {'6','5'}, {'6','6'}, {'6','7'}, {'6','8'}, {'6','9'}, {'6','a'}, {'6','b'}, {'6','c'}, {'6','d'}, {'6','e'}, {'6','f'},
    {'7','0'}, {'7','1'}, {'7','2'}, {'7','3'}, {'7','4'}, {'7','5'}, {'7','6'}, {'7','7'}, {'7','8'}, {'7','9'}, {'7','a'}, {'7','b'}, {'7','c'}, {'7','d'}, {'7','e'}, {'7','f'},
    {'8','0'}, {'8','1'}, {'8','2'}, {'8','3'}, {'8','4'}, {'8','5'}, {'8','6'}, {'8','7'}, {'8','8'}, {'8','9'}, {'8','a'}, {'8','b'}, {'8','c'}, {'8','d'}, {'8','e'}, {'8','f'},
    {'9','0'}, {'9','1'}, {'9','2'}, {'9','3'}, {'9','4'}, {'9','5'}, {'9','6'}, {'9','7'}, {'9','8'}, {'9','9'}, {'9','a'}, {'9','b'}, {'9','c'}, {'9','d'}, {'9','e'}, {'9','f'},
    {'a','0'}, {'a','1'}, {'a','2'}, {'a','3'}, {'a','4'}, {'a','5'}, {'a','6'}, {'a','7'}, {'a','8'}, {'a','9'}, {'a','a'}, {'a','b'}, {'a','c'}, {'a','d'}, {'a','e'}, {'a','f'},
    {'b','0'}, {'b','1'}, {'b','2'}, {'b','3'}, {'b','4'}, {'b','5'}, {'b','6'}, {'b','7'}, {'b','8'}, {'b','9'}, {'b','a'}, {'b','b'}, {'b','c'}, {'b','d'}, {'b','e'}, {'b','f'},
    {'c','0'}, {'c','1'}, {'c','2'}, {'c','3'}, {'c','4'}, {'c','5'}, {'c','6'}, {'c','7'}, {'c','8'}, {'c','9'}, {'c','a'}, {'c','b'}, {'c','c'}, {'c','d'}, {'c','e'}, {'c','f'},
    {'d','0'}, {'d','1'}, {'d','2'}, {'d','3'}, {'d','4'}, {'d','5'}, {'d','6'}, {'d','7'}, {'d','8'}, {'d','9'}, {'d','a'}, {'d','b'}, {'d','c'}, {'d','d'}, {'d','e'}, {'d','f'},
    {'e','0'}, {'e','1'}, {'e','2'}, {'e','3'}, {'e','4'}, {'e','5'}, {'e','6'}, {'e','7'}, {'e','8'}, {'e','9'}, {'e','a'}, {'e','b'}, {'e','c'}, {'e','d'}, {'e','e'}, {'e','f'},
    {'f','0'}, {'f','1'}, {'f','2'}, {'f','3'}, {'f','4'}, {'f','5'}, {'f','6'}, {'f','7'}, {'f','8'}, {'f','9'}, {'f','a'}, {'f','b'}, {'f','c'}, {'f','d'}, {'f','e'}, {'f','f'},
};

void keyToString( const Key& key, KeyString& keyStr ) {
    for( uint i=0; i<key.size(); i++ ) {
        keyStr[2*i]   = byteMap[key[i]][0];
        keyStr[2*i+1] = byteMap[key[i]][1];
    }
    keyStr[2*Key_Size]=0;
}

// DefaultDrive
class DefaultDrive: public Drive {
    fs::path    m_rootPath;
    Key         m_drivePubKey;

    std::shared_ptr<FileTransmitter> m_fileTransmitter;

    KeyString   m_driveKeyString;
    fs::path    m_drivePath;
    fs::path    m_fsTreeFile;
    fs::path    m_tmpDrivePath;
    fs::path    m_tmpFsTreeFile;
    fs::path    m_tmpActionListFile;

    //FsTree      m_fsTree; //?
    FsTree      m_newFsTree;

    ActionList  m_actionList;
    uint        m_currentActionIndex;

public:

    DefaultDrive( std::string rootPath ) : m_rootPath(rootPath) {}

    virtual ~DefaultDrive() {}

    void init( Key      drivePubKey,
               size_t   maxDriveSize,
               std::shared_ptr<FileTransmitter> fileTransmitter ) override
    {
        m_drivePubKey = drivePubKey;
        m_fileTransmitter = fileTransmitter;

        keyToString( m_drivePubKey, m_driveKeyString );
        m_drivePath = fs::path( m_rootPath );
        m_drivePath /= m_driveKeyString;

        m_fsTreeFile = fs::path( m_drivePath );
        m_fsTreeFile.replace_extension(".fsTree");

        m_tmpDrivePath = fs::path( m_drivePath );
        m_tmpDrivePath.replace_extension(".tmp");

        m_tmpFsTreeFile = fs::path( m_drivePath );
        m_tmpFsTreeFile.replace_extension(".fsTree");

        m_tmpActionListFile = fs::path( m_drivePath );
        m_tmpActionListFile.replace_extension(".tmpActionList");

        //TODO load drive structure for libtorrent?
    }

    void executeActionList( FileHash actionListHash ) override {

        // clear tmp object
        fs::remove_all( m_tmpDrivePath );
        fs::create_directory( m_tmpDrivePath );
        fs::remove_all( m_tmpFsTreeFile );
        fs::remove_all( m_tmpActionListFile );

        // initialize new fsTree
        m_newFsTree.deserialize( m_fsTreeFile );

        // start upload of the action list
        m_fileTransmitter->download( actionListHash, m_tmpDrivePath.string(),
                                     std::bind( &DefaultDrive::handleActionList, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3 ) );
    }

    void handleActionList( download_status::code code, FileHash, const std::string& fileName ) {

        if ( code == download_status::failed ) {
            //TODO cancel "modify drive"
            return;
        }

        if ( code == download_status::complete ) {
            //TODO check action list hash
            m_actionList.deserialize( "m_tmpActionListFile.string()" );
            m_currentActionIndex = 0;
            exectuteAction();
        }

    }

    void exectuteAction() {

//        for( ; m_currentActionIndex < m_actionList.size(); m_currentActionIndex++ )
//        {
//            //TODO const
//            Action& action = m_actionList[m_currentActionIndex];
//            switch( action.m_actionId )
//            {
//            case action_list_id::upload: {
//                //fs::path filePath = action.m_param1;
//                //TODO
//                std::string outputFolder = "???";
//                m_fileTransmitter->download( action.m_hash, outputFolder, std::bind( &DefaultDrive::handleUnploadFile, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3 ) );
//                return;
//            }
//            case action_list_id::new_folder:
//                m_newFsTree.addFolder( action.m_param1 );
//                break;
//            case action_list_id::rename:
//                m_newFsTree.move( action.m_param1, action.m_param2 );
//                break;
//            case action_list_id::remove:
//                m_newFsTree.remove( action.m_param1 );
//                break;
//            case action_list_id::none:
//                break;
//            }
//        }
        //TODO
    }

    void handleUnploadFile( download_status::code code, FileHash, const std::string& fileName ) {

        if ( code == download_status::failed ) {
            //TODO cancel "modify drive"
            return;
        }

        if ( code == download_status::complete ) {
            //TODO
        }

    }


    bool createDriveStruct( FsTree& node, const std::string& path, const std::string& logicalPath ) override
    {
        return true;
    }

};

std::shared_ptr<Drive> createDefaultDrive( std::string rootPath ) {
    return std::make_shared<DefaultDrive>( rootPath );
}

}
