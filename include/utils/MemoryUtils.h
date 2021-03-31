#pragma once
#include <memory>
#include <forward_list>
#include "model/EntityPtr.h"
#include "exceptions.h"

namespace sirius { namespace utils {
    /// Creates a unique pointer of the specified type with custom \a size.
    template<typename T>
    model::UniqueEntityPtr<T> MakeUniqueWithSize(size_t size) {
        if (size < sizeof(T))
        CATAPULT_THROW_INVALID_ARGUMENT("size is insufficient");

        return model::UniqueEntityPtr<T>(static_cast<T*>(::operator new(size)));
    }

        /// Creates a shared pointer of the specified type with custom \a size.
    template<typename T>
    std::shared_ptr<T> MakeSharedWithSize(size_t size) {
        if (size < sizeof(T))
            CATAPULT_THROW_INVALID_ARGUMENT("size is insufficient");

        return std::shared_ptr<T>(reinterpret_cast<T*>(::operator new(size)), model::EntityPtrDeleter<T>{});
    }
}}