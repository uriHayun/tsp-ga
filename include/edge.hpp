#pragma once

#include <cstddef>

// Represents an edge between 2 cities (e.g., (4, 6))
struct Edge {
    std::size_t from;
    std::size_t to;
};