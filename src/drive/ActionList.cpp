/*
*** Copyright 2021 ProximaX Limited. All rights reserved.
*** Use of this source code is governed by the Apache 2.0
*** license that can be found in the LICENSE file.
*/

#include "drive/ActionList.h"
#include <fstream>
#include <cereal/types/memory.hpp>
#include <cereal/archives/portable_binary.hpp>

namespace sirius { namespace drive {

	void ActionList::serialize(const std::string& fileName) const
	{
		std::ofstream os(fileName, std::ios::binary);
		cereal::PortableBinaryOutputArchive archive(os);
		archive(size());
		for(uint i = 0; i < size(); i++) {
			archive(at(i));
		}
	}

	void ActionList::deserialize(const std::string& fileName)
	{
		clear();
		std::ifstream is(fileName, std::ios::binary);
		cereal::PortableBinaryInputArchive archive(is);
		size_t size;
		archive(size);
		reserve(size);
		for (uint i = 0; i < size; i++) {
			push_back(Action());
			archive(at(i));
		}
	}
}}
