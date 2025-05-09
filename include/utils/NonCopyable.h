/**
*** Copyright (c) 2016-present,
*** Jaguar0625, gimre, BloodyRookie, Tech Bureau, Corp. All rights reserved.
***
*** This file is part of Catapult.
***
*** Catapult is free software: you can redistribute it and/or modify
*** it under the terms of the GNU Lesser General Public License as published by
*** the Free Software Foundation, either version 3 of the License, or
*** (at your option) any later version.
***
*** Catapult is distributed in the hope that it will be useful,
*** but WITHOUT ANY WARRANTY; without even the implied warranty of
*** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
*** GNU Lesser General Public License for more details.
***
*** You should have received a copy of the GNU Lesser General Public License
*** along with Catapult. If not, see <http://www.gnu.org/licenses/>.
**/

#pragma once
#include "plugins.h"

namespace sirius { namespace utils {

        /// A class that can neither be copied nor moved.
        class PLUGIN_API NonCopyable {
        public:
            /// Default constructor.
            constexpr NonCopyable() = default;

            /// Default destructor.
            ~NonCopyable() = default;

        public:
            /// Disabled copy constructor.
            NonCopyable(const NonCopyable&) = delete;

            /// Disabled assignment operator.
            NonCopyable& operator=(const NonCopyable&) = delete;
        };

        /// A class that can be moved but not copied.
        class PLUGIN_API MoveOnly {
        public:
            /// Default constructor.
            constexpr MoveOnly() = default;

            /// Default destructor.
            ~MoveOnly() = default;

        public:
            /// Disabled copy constructor.
            MoveOnly(const NonCopyable&) = delete;

            /// Default move constructor.
            MoveOnly(MoveOnly&&) = default;

            /// Disabled assignment operator.
            MoveOnly& operator=(const MoveOnly&) = delete;

            /// Default move assignment operator.
            MoveOnly& operator=(MoveOnly&&) = default;
        };
    }}
