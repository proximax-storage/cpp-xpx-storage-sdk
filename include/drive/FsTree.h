/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#pragma once
#include "types.h"
#include <vector>
#include <set>
#include <variant>
#include <cereal/archives/binary.hpp>

namespace sirius { namespace drive {

    // File
    struct File {

        template <class Archive> void serialize(Archive & arch) {
            arch(m_name);
            arch(cereal::binary_data(m_hash.data(), m_hash.size()));
            arch(m_size);
        }

#ifdef DEBUG
        bool operator==(const File& f) const { return m_name==f.m_name && m_hash==f.m_hash; }
#endif

        std::string m_name;
        Hash256    m_hash;
        size_t      m_size;
    };

    struct Folder {

        using Child = std::variant<Folder,File>;

        bool initWithFolder(const std::string& pathToFolder);
        void sort();
        void dbgPrint(std::string leadingSpaces = "") const;


        template <class Archive> void serialize(Archive & arch) {
            arch(m_name);
            arch(m_childs);
        }

#ifdef DEBUG
        bool operator==(const Folder& f) const { return m_name==f.m_name && m_childs==f.m_childs; }
#endif

        std::string         m_name;
        std::vector<Child>  m_childs;
    };

    // variant utilities
    inline bool          isFolder(const Folder::Child& child)  { return child.index()==0; }
    inline const Folder& getFolder(const Folder::Child& child) { return std::get<0>(child); }
    inline       Folder& getFolder(Folder::Child& child)       { return std::get<0>(child); }
    inline const File&   getFile(const Folder::Child& child)   { return std::get<1>(child); }
    inline       File&   getFile(Folder::Child& child)         { return std::get<1>(child); }

    // operator< (used for sorting)
    inline bool operator<(const Folder::Child& a, const Folder::Child& b) {
        if (isFolder(a)) {
            if (!isFolder(b))
                return true;
            return getFolder(a).m_name < getFolder(b).m_name;
        }
        else {
            if (isFolder(b))
                return false;
            return getFile(a).m_name < getFile(b).m_name;
        }
    }

	struct FsTree: public Folder {

		Hash256 doSerialize(std::string fileName);
		void     deserialize(std::string fileName);

		Folder*  getFolderPtr(const std::string& path, bool createIfNotExist = false);

		bool     addFile(const std::string& destinationPath, const std::string& filename, const Hash256&, size_t size);

		bool     addFolder(const std::string& folderPath);

		bool     remove(const std::string& path);

		bool     move(const std::string& oldPathAndName, const std::string& newPathAndName);
	};
}}

