#pragma once

#include "assert.hpp"

#include <cstddef>

namespace util {

template<typename T>
class NonNullPtr
{
public:
    NonNullPtr(T& ref)
        : Raw_(&ref)
    { }

    explicit NonNullPtr(T* raw)
        : Raw_(raw)
    {
        ASSERT(Raw_ != nullptr);
    }

    operator T*() const
    {
        return Raw_;
    }

    auto operator->() const
    {
        return Raw_;
    }

private:
    T* Raw_;
};

template<typename T>
bool operator==(NonNullPtr<T> ptr, std::nullptr_t nptr)
{
    return false;
}

} // namespace util
