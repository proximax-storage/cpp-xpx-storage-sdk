/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/FsTree.h"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <cereal/types/vector.hpp>
#include <cereal/types/memory.hpp>
#include <cereal/archives/binary.hpp>

namespace sirius { namespace drive {

	void Folder::dbgPrint(std::string leadingSpaces) const {

		std::cout << leadingSpaces << "• " << m_name << std::endl;

		for(auto it = m_childs.begin(); it != m_childs.end(); it++) {

			if (isFolder(*it)) {
				getFolder(*it).dbgPrint(leadingSpaces+"  ");
			}
			else {
				std::cout << leadingSpaces << "  " << getFile(*it).m_name << std::endl;
			}
		}
	}

	// sort
	void Folder::sort() {
		std::sort(m_childs.begin(), m_childs.end());

		for(auto it = m_childs.begin(); it != m_childs.end(); it++) {
			if (isFolder(*it)) {
				getFolder(*it).sort();
			}
		}
	}

	// initWithFolder
	bool Folder::initWithFolder(const std::string& pathToFolder) try {

	#ifdef DEBUG
		std::srand(unsigned(std::time(nullptr)));
	#endif

		m_childs.clear();
		m_name = std::filesystem::path(pathToFolder).filename();

		for (const auto& entry : std::filesystem::directory_iterator(pathToFolder)) {

			const auto entryName = entry.path().filename().string();

			if (entry.is_directory()) {
				//std::cout << "dir:  " << filenameStr << '\n';

				Folder subfolder{};

				//TODO Windows path!
				if (!subfolder.initWithFolder(pathToFolder+"/"+entryName))
					return false;

				m_childs.push_back(subfolder);
				//m_childs.insert(subfolder);
			}
			else if (entry.is_regular_file()) {
				//std::cout << "file: " << filenameStr << '\n';

				if (entryName != ".DS_Store")
				{
#ifdef DEBUG
					Hash256 fileHash;
					std::generate(fileHash.begin(), fileHash.end(), std::rand);
					//m_childs.insert(  );
					m_childs.emplace_back(File{entryName,fileHash,entry.file_size()});
#else
	                m_childs.emplace_back(File{entryName, Hash256(), 0u});
					//m_childs.insert(File{entryName});

#endif
				}
			}
		}

		return true;
	}
	catch(...)
	{
		return false;
	}

	// doSerialize
	Hash256 FsTree::doSerialize(std::string fileName) {
		std::ofstream os(fileName, std::ios::binary);
		cereal::BinaryOutputArchive archive(os);

		// sort tree before saving
		sort();
//		archive(*this);

		//TODO
		return Hash256();
	}

	// deserialize
	void FsTree::deserialize(std::string fileName) {
		m_childs.clear();
		std::ifstream is(fileName, std::ios::binary);
		cereal::BinaryInputArchive iarchive(is);
//		iarchive(*this);
	}

	// addFile
	bool FsTree::addFile(const std::string& destinationPath, const std::string& filename, const Hash256& fileHash, size_t size) {

		Folder* parentFolder = getFolderPtr(destinationPath, true);

		if (parentFolder == nullptr)
			return false;

		parentFolder->m_childs.emplace_back(File{filename,fileHash,size});

		return true;
	}

	// addFolder
	bool FsTree::addFolder(const std::string& folderPath) {

		Folder* parentFolder = getFolderPtr(folderPath, true);

		return parentFolder != nullptr;
	}

	// remove
	bool FsTree::remove(const std::string& fullPath) {

		std::filesystem::path path(fullPath);
		std::string filename = path.filename().string();
		Folder* parentFolder = getFolderPtr(path.parent_path().string());

		auto it = std::find_if(parentFolder->m_childs.begin(), parentFolder->m_childs.end(),
							 [=](const Child& child) -> bool
							 {
								 if (isFolder(child))
									 return getFolder(child).m_name == filename;
								 return getFile(child).m_name == filename;
							 });

		if (it == parentFolder->m_childs.end())
			return false;

		parentFolder->m_childs.erase(it);

		return true;
	}

	// move
	bool FsTree::move(const std::string& oldPathAndName, const std::string& newPathAndName)
	{
		if (std::filesystem::path(newPathAndName) == std::filesystem::path(oldPathAndName))
			return true;

		std::filesystem::path path(oldPathAndName);
		std::string filename = path.filename().string();
		Folder* parentFolder = getFolderPtr(path.parent_path().string());

		auto it = std::find_if(parentFolder->m_childs.begin(), parentFolder->m_childs.end(),
							 [=](const Child& child) -> bool
							 {
								 if (isFolder(child))
									 return getFolder(child).m_name == filename;
								 return getFile(child).m_name == filename;
							 });

		if (it == parentFolder->m_childs.end())
			return false;

		std::filesystem::path newPath(newPathAndName);
		std::string newFilename = newPath.filename().string();
		Folder* newParentFolder = getFolderPtr(newPath.parent_path().string(), true);

		auto newIt = std::find_if(newParentFolder->m_childs.begin(), newParentFolder->m_childs.end(),
							 [=](const Child& child) -> bool
							 {
								 if (isFolder(child))
									 return getFolder(child).m_name == newFilename;
								 return getFile(child).m_name == newFilename;
							 });

		// newPathAndName should not exist
		if (newIt != newParentFolder->m_childs.end())
			return false;

		newParentFolder->m_childs.emplace_back(*it);
		parentFolder->m_childs.erase(it);

		return true;
	}

	Folder* FsTree::getFolderPtr(const std::string& fullPath, bool createIfNotExist)
	{
		std::filesystem::path path(fullPath);
		Folder* treeWalker = this;

		for(auto pathIt = path.begin(); pathIt != path.end(); pathIt++) {

			auto it = std::find_if(treeWalker->m_childs.begin(), treeWalker->m_childs.end(),
								 [=](const Child& child) -> bool
								 {
									 return isFolder(child) && getFolder(child).m_name == pathIt->string();
								 });

			if (it == treeWalker->m_childs.end())
			{
				if (!createIfNotExist)
					return nullptr;

				treeWalker->m_childs.emplace_back(Folder{pathIt->string(), {}});
				treeWalker = &getFolder(treeWalker->m_childs.back());
			}
			else if (isFolder(*it))
			{
				treeWalker = &getFolder(*it);
			}
			else
			{
				//TODO invalid path
				return nullptr;
			}
		}

		return treeWalker;
	}
}}
