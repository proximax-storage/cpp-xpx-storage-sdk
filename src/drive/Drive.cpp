/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/Drive.h"
#include "drive/FileTransmitter.h"
#include "drive/Utils.h"
#include <filesystem>
#include <iostream>

namespace sirius { namespace drive {

	// DefaultDrive
	class DefaultDrive: public Drive, public std::enable_shared_from_this<DefaultDrive> {
	public:
		DefaultDrive(std::string rootPath) : m_rootPath(rootPath) {}
		virtual ~DefaultDrive() {}

	public:
		void init(const Key& drivePubKey, size_t maxDriveSize, std::shared_ptr<FileTransmitter> pFileTransmitter) override {
			(void)maxDriveSize;

			m_drivePubKey = drivePubKey;
			m_pFileTransmitter = std::move(pFileTransmitter);

			m_drivePath = std::filesystem::path(m_rootPath);
			m_drivePath /= ToString(m_drivePubKey);

			m_fsTreeFile = std::filesystem::path(m_drivePath);
			m_fsTreeFile.replace_extension(".fsTree");

			m_tmpDrivePath = std::filesystem::path(m_drivePath);
			m_tmpDrivePath.replace_extension(".tmp");

			m_tmpFsTreeFile = std::filesystem::path(m_drivePath);
			m_tmpFsTreeFile.replace_extension(".fsTree");

			m_tmpActionListFile = std::filesystem::path(m_drivePath);
			m_tmpActionListFile.replace_extension(".tmpActionList");

			//TODO load drive structure for libtorrent?
		}

		void executeActionList(const Hash256& actionListHash) override {

			// clear tmp object
			std::filesystem::remove_all(m_tmpDrivePath);
			std::filesystem::create_directory(m_tmpDrivePath);
			std::filesystem::remove_all(m_tmpFsTreeFile);
			std::filesystem::remove_all(m_tmpActionListFile);

			// initialize new fsTree
			m_newFsTree.deserialize(m_fsTreeFile);

			// start upload of the action list
			m_pFileTransmitter->download(
				actionListHash,
				m_tmpDrivePath.string(),
				[pThis = shared_from_this()](download_status::code code, const Hash256& hash, const std::string& info) {
					pThis->handleActionList(code, hash, info);
				});
		}

		void handleActionList(download_status::code code, const Hash256&, const std::string& fileName) {
			(void)fileName;

			if (code == download_status::failed) {
				//TODO cancel "modify drive"
				return;
			}

			if (code == download_status::complete) {
				//TODO check action list hash
				m_actionList.deserialize(m_tmpActionListFile.string());
				m_currentActionIndex = 0;
				exectuteAction();
			}

		}

		void exectuteAction() {

//        for(; m_currentActionIndex < m_actionList.size(); m_currentActionIndex++)
//        {
//            //TODO const
//            Action& action = m_actionList[m_currentActionIndex];
//            switch(action.m_actionId)
//            {
//            case action_list_id::upload: {
//                //std::filesystem::path filePath = action.m_param1;
//                //TODO
//                std::string outputFolder = "???";
//                m_fileTransmitter->download(action.m_hash, outputFolder, std::bind(&DefaultDrive::handleUnploadFile, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
//                return;
//            }
//            case action_list_id::new_folder:
//                m_newFsTree.addFolder(action.m_param1);
//                break;
//            case action_list_id::rename:
//                m_newFsTree.move(action.m_param1, action.m_param2);
//                break;
//            case action_list_id::remove:
//                m_newFsTree.remove(action.m_param1);
//                break;
//            case action_list_id::none:
//                break;
//            }
//        }
        //TODO
		}

		void handleUnploadFile(download_status::code code, const Hash256&, const std::string& fileName) {
			(void)fileName;

			if (code == download_status::failed) {
				//TODO cancel "modify drive"
				return;
			}

			if (code == download_status::complete) {
				//TODO
			}

		}


		bool createDriveStruct(const FsTree&, const std::string&, const std::string&) override {
			return true;
		}

	private:
		std::filesystem::path m_rootPath;
		Key m_drivePubKey;

		std::shared_ptr<FileTransmitter> m_pFileTransmitter;

		std::filesystem::path m_drivePath;
		std::filesystem::path m_fsTreeFile;
		std::filesystem::path m_tmpDrivePath;
		std::filesystem::path m_tmpFsTreeFile;
		std::filesystem::path m_tmpActionListFile;

		FsTree m_newFsTree;

		ActionList m_actionList;
		uint m_currentActionIndex;
	};

	std::shared_ptr<Drive> СreateDefaultDrive(std::string rootPath) {
		return std::make_shared<DefaultDrive>(rootPath);
	}
}}
