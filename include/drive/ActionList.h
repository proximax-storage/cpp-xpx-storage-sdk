/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/
#pragma once
#include "types.h"
#include <string>
#include <utility>
#include <vector>

namespace sirius { namespace drive {

    namespace action_list_id {
        enum code
        {
            none        = 0,
            upload      = 1,
            new_folder  = 2,
            rename      = 3,
            remove      = 4,
        };
    }

    class Action {
	public:
        Action() = default;
        Action(action_list_id::code code, std::string currentPath, std::string newPath)
        	: m_id(code)
        	, m_currentPath(std::move(currentPath))
        	, m_newPath(std::move(newPath)) {}

        static Action upload(std::string currentPath, std::string newPath) {
            return Action(action_list_id::upload, std::move(currentPath), std::move(newPath));
        }

        static Action newFolder(std::string remoteFolderPath) {
            return Action(action_list_id::new_folder, "", std::move(remoteFolderPath));
        }

        static Action rename(std::string currentPath, std::string newPath) {
            return Action(action_list_id::rename, std::move(currentPath), std::move(newPath));
        }

        static Action remove(std::string newPath) {
            return Action(action_list_id::remove, std::move(newPath), "");
        }

	public:
    	auto id() const {
			return m_id;
        }

    	auto currentPath() const {
			return m_currentPath;
        }

    	auto newPath() const {
			return m_newPath;
        }

        template<class Archive>
        void serialize(Archive& arch) {
            arch(m_id);
			arch(m_currentPath);
			arch(m_newPath);
        }

        bool operator==(const Action& a) const { return m_id == a.m_id && m_currentPath == a.m_currentPath && m_newPath == a.m_newPath; }

	private:
        action_list_id::code m_id = action_list_id::none;
        std::string m_currentPath;
        std::string m_newPath;
    };

    class ActionList : public std::vector<Action> {
	public:
        void serialize(const std::string& fileName) const;
        void deserialize(const std::string& fileName);
    };
}}
