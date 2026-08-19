#pragma once

#include "tour.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <random>
#include <unordered_set>
#include <utility>
#include <vector>

namespace Eax {

Tour crossover(const Tour &parent_a, const Tour &parent_b);

namespace Detail {

// Represents an edge between 2 cities (e.g., (4, 6))
struct Edge {
    int from;
    int to;
};

// Labels whether edge is from parent A/B
enum class Parent {
    A,
    B
};

// Associates an ordinary edge with its originating parent
struct TaggedEdge {
    Edge edge;
    Parent parent;
};

// Measurements of how cycles relate to each other
struct AbCycleWeights {
    // Number of cities cycle "i" shares with other cycles in common
    std::vector<int> shared_cities_total;

    // Number of cities cycles "i" and "j" have in common
    std::vector<std::vector<int>> shared_cities_between;
};

// Hash function for using a tagged-edge-key in an unordered_set
struct TaggedEdgeHash {
    std::size_t operator()(const TaggedEdge &te) const noexcept;
};

// Equality function for using a TaggedEdge in an unordered_set
struct TaggedEdgeEqual {
    bool operator()(const TaggedEdge &lhs, const TaggedEdge &rhs) const noexcept;
};

using EdgeKey = std::pair<int, int>;

// Hash function for using an edge-key in an unordered_set
struct EdgeHash {
    std::size_t operator()(const EdgeKey &key) const noexcept;
};

using EdgeSet = std::unordered_set<EdgeKey, EdgeHash>;
using TaggedEdgeSet = std::unordered_set<TaggedEdge, TaggedEdgeHash, TaggedEdgeEqual>;
using Edges = std::vector<Edge>;
using TaggedEdges = std::vector<TaggedEdge>;
using AbGraph = std::vector<std::vector<TaggedEdge>>;
using AbCycle = std::vector<TaggedEdge>;
using AbCycles = std::vector<AbCycle>;  // Full set of AB-cycles from the AB-graph
using ESet = std::vector<AbCycle>;  // Selected subset forming the E-set

// Extracts all edges from a tour
Edges get_edges(const Tour &tour);

// Returns the edges unique to the source collection
TaggedEdges tag_edges_with_parent(
    const Edges &edges_a,
    const Edges &edges_b);

// Tags each edge with its originating parent
AbGraph build_ab_graph(
    const TaggedEdges &edges,
    std::size_t num_cities);

// Transforms the AB-graph into AB-cycles by repeatedly walking an alternating
// path between A/B edges from an arbitrary edge until returning to the starting
// city
AbCycles get_ab_cycles(
    const AbGraph &graph,
    const TaggedEdges &edges);

// Builds measurements (weights) for each cycle based on their relationships
AbCycleWeights build_ab_cycle_weights(
    const AbCycles &cycles,
    std::size_t num_cities);

// Gets half the number of edges for each cycle
std::vector<int> get_cycle_half_edge_counts(const AbCycles &cycles);

// Selects a subset (E-set) of AB-cycles to form the E-set for the crossover operation
ESet select_e_set(
    const AbCycles &cycles,
    const AbCycleWeights &weights,
    const std::vector<int> &cycle_half_edge_counts,
    std::mt19937 &rng);

// Minimizes number of conflicting cities in the E-set 
// by iteratively adding/removing cycles
std::vector<int> improve_e_set(
    int anchor_cycle_idx,
    const std::vector<int> &initial_cycles,
    const std::vector<int> &shared_cities_total,
    const std::vector<std::vector<int>> &shared_cities_between,
    const std::vector<int> &cycle_half_edge_counts,
    std::mt19937 &rng,
    const int max_consecutive_non_improving_iter_count = 20);

// Returns the canonical representation of an edge
EdgeKey normalize_edge(const Edge &edge);

// Checks whether an edge exists in an edge-set
bool contains_edge(const EdgeSet &edge_set, const Edge &edge);

// Builds a hash set for fast edge lookups
EdgeSet build_edge_set(const Edges &edges);

// Returns the edges unique to the source collection.
Edges get_unique_edges(
    const Edges &src,
    const Edges &other);

// Given endpoint of an edge, returns other endpoint
int get_other_endpoint(const TaggedEdge &te, int curr_city);

// Builds the initial collection of unused edges,
// each edge being tracked once rather than once per each endpoint
TaggedEdgeSet build_unused_edges_set(const TaggedEdges &edges);

// Selects a subset (E-set) of AB-cycles randomly
ESet select_e_set_rand(
    const AbCycles &cycles,
    std::mt19937 &rng,
    double inclusion_prob = 0.5);
}

}