#pragma once

#include <memory_resource>
#include <vector>

template <typename T>
using Vector = std::pmr::vector<T>;

using Arena = std::pmr::monotonic_buffer_resource;
