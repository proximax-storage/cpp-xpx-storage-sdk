#pragma once
#include <memory>
#include <forward_list>
#include "catapult/model/EntityPtr.h"

namespace catapult { namespace utils {
    /// Creates a shared pointer of the specified type with custom \a size.
    template<typename T>
    std::shared_ptr<T> MakeSharedWithSize(size_t size) {
        if (size < sizeof(T))
            throw("size is insufficient");

        return std::shared_ptr<T>(reinterpret_cast<T*>(::operator new(size)), model::EntityPtrDeleter<T>{});
    }
}}