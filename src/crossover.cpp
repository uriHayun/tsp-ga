#include "crossover.hpp"
#include "tour.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <functional>
#include <limits>
#include <random>
#include <vector>

namespace Eax {

// Performs Edge Assembly Crossover (EAX) between 2 parent tours
// and returns the resulting offspring
Tour crossover(const Tour &parent_a, const Tour &parent_b) {

    using namespace Detail;

    // STEP 1: build temporary AB-graph to produce AB-cycles (used in STEP 2 choose the E-set)

    const std::vector<Edge> edges_a = get_edges(parent_a);
    const std::vector<Edge> edges_b = get_edges(parent_b);

    const TaggedEdges tagged_edges = tag_edges_with_parent(edges_a, edges_b);

    const AbGraph graph = build_ab_graph(tagged_edges, parent_a.size());

    const std::vector<AbCycle> cycles = get_ab_cycles(graph, tagged_edges);

    // STEP 2: select a subset (E-set) of AB-cycles to form the E-set for the crossover operation

    const AbCycleWeights weights = build_ab_cycle_weights(cycles, parent_a.size());
    
    const std::vector<int> cycle_half_edge_counts = get_cycle_half_edge_counts(cycles);

    std::random_device rd;
    std::mt19937 rng(rd());

    const std::vector<AbCycle> e_set = select_e_set(cycles, weights, cycle_half_edge_counts, rng);

    // STEP 3: TODO

    return {};
}

namespace Detail {

// Hash function for using an Edge in an unordered_set
std::size_t EdgeHash::operator()(const EdgeKey &key) const noexcept {
    std::size_t h1 = std::hash<int>{}(key.first);
    std::size_t h2 = std::hash<int>{}(key.second);

    return h1 ^ (h2 << 1);
}

// Extracts all edges from a tour, including the closing edge 
// from the last city to the first using modulo the tour length
// e.g., { 1, 4, 2, 3 } to { (1, 4), (4, 2), (2, 3), (3, 1) }
std::vector<Edge> get_edges(const Tour &tour) {

    std::vector<Edge> edges;
    edges.reserve(tour.size());

    for (std::size_t from_idx = 0, N = tour.size(); from_idx < N; from_idx++) {
        std::size_t to_idx = (from_idx + 1) % N;

        edges.push_back({tour[from_idx], tour[to_idx]});
    }

    return edges;
}

// Return a canonical representation of an edge
// so that (t, z) and (z, t) are treated as identical
EdgeKey normalize_edge(const Edge &edge) {
    return {
        std::min(edge.from, edge.to),
        std::max(edge.from, edge.to)
    };
}

// Hash function for using a TaggedEdge in an unordered_set
std::size_t TaggedEdgeHash::operator()(const TaggedEdge &te) const noexcept {
    const std::size_t h1 = EdgeHash{}(normalize_edge(te.edge));
    const std::size_t h2 = std::hash<int>{}(static_cast<int>(te.parent));

    return h1 ^ (h2 << 1);
}

// Equality function for using a TaggedEdge in an unordered_set
// 2 TaggedEdges are considered equal if:
// connect the same cities + come from the same parent
bool TaggedEdgeEqual::operator()(
    const TaggedEdge &lhs,
    const TaggedEdge &rhs) const noexcept {
    return normalize_edge(lhs.edge) == normalize_edge(rhs.edge)
        && lhs.parent == rhs.parent;
}

// Returns whether an edge exists in an edge-set using an
// average O(1) hash-table lookup
bool contains_edge(const EdgeSet &edge_set, const Edge &edge) {
    return edge_set.contains(normalize_edge(edge));
}

// Builds a hash set of edges for fast memory lookups
EdgeSet build_edge_set(const std::vector<Edge> &edges) {
    EdgeSet edge_set;

    for (const Edge &edge : edges) {
        edge_set.insert(normalize_edge(edge));
    }

    return edge_set;
}

// Return edges appear in "src" which do not appear in "other"
std::vector<Edge> get_unique_edges(
    const std::vector<Edge> &src,
    const std::vector<Edge> &other) {

        EdgeSet other_set = build_edge_set(other);

        std::vector<Edge> unique_edges;
        unique_edges.reserve(src.size());

        for (const Edge &edge : src) {
            if (!contains_edge(other_set, edge)) {
                unique_edges.push_back(edge);
            }
        }

        return unique_edges;
    }

// Lables each edge with the parent (A or B) it originated from
TaggedEdges tag_edges_with_parent(
    const std::vector<Edge> &edges_a,
    const std::vector<Edge> &edges_b) {

        TaggedEdges edges;
        edges.reserve(edges_a.size() + edges_b.size());

        for (const Edge &edge : edges_a) {
            edges.push_back({edge, Parent::A});
        }

        for (const Edge &edge : edges_b) {
            edges.push_back({edge, Parent::B});
        }

        return edges;
    }

// Builds the AB adjacency graph used to search for AB-cycles
// Contains just A-only edges or B-only edges, does not contain A and B shared edges
// Example AB adjacency graph:

// graph[0] = {
//     { {0, 1}, Parent::A },
//     { {1, 4}, Parent::B }
// };
AbGraph build_ab_graph(
    const std::vector<TaggedEdge> &edges,
    std::size_t num_cities) {
        AbGraph graph(num_cities);

        for (const TaggedEdge &edge : edges) {
            // Store each edge for both edgepoints
            // so the graph can be traversed from either city
            graph[edge.edge.from].push_back(edge);
            graph[edge.edge.to].push_back(edge);
        }

        return graph;
    }

// Given endpoint of an edge, returns other endpoint
int get_other_endpoint(const TaggedEdge &te, int curr_city) {
    return (te.edge.from == curr_city)
        ? te.edge.to
        : te.edge.from;
}

// Builds the initial collection of unused edges,
// each edge being tracked once rather than once per endpoint:
// (if edge (t, z) exists, there's no need for edge (z, t))
TaggedEdgeSet build_unused_edges_set(const std::vector<TaggedEdge> &edges) {
    TaggedEdgeSet unused_edges;
    unused_edges.reserve(edges.size());

    for (const TaggedEdge &edge : edges) {
        unused_edges.insert(edge);
    }

    return unused_edges;
}

// Transforms the AB-graph into AB-cycles by repeatedly walking an alternating
// path between A/B edges from an arbitrary edge until returning to the starting
// city
std::vector<AbCycle> get_ab_cycles(
    const AbGraph &graph,
    const TaggedEdges &edges) {

    std::vector<AbCycle> cycles;

    TaggedEdgeSet unused_edges = build_unused_edges_set(edges);

    while (!unused_edges.empty()) {
        auto it = unused_edges.begin();
        TaggedEdge start_edge = *it;
        
        int start_city = start_edge.edge.from;

        AbCycle curr_cycle;

        TaggedEdge curr_edge = start_edge;
        int curr_city = curr_edge.edge.from;

        while (true) {
            curr_cycle.push_back(curr_edge);
            unused_edges.erase(curr_edge);
            
            curr_city = get_other_endpoint(curr_edge, curr_city);
            if (curr_city == start_city) {
                break;
            }

            Parent expected_parent = (curr_edge.parent == Parent::A)
                ? Parent::B
                : Parent::A;

            // Track whether an adequate (unused of expected parent from currCity) edge was found
            bool found_next = false;
            for (const TaggedEdge &edge : graph[curr_city]) {
                if (edge.parent != expected_parent || !unused_edges.contains(edge)) {
                    continue;
                }

                curr_edge = edge;
                found_next = true;
                break;
            }

            assert(found_next && "AB-cycle failed to close: dead end before returning to start city");
        }

        cycles.push_back(curr_cycle);
    }
    
    return cycles;
}

// Selects a subset (E-set) of AB-cycles randomly
std::vector<AbCycle> select_e_set_rand(
    const std::vector<AbCycle> &cycles,
    std::mt19937 &rng,
    double inclusion_prob) {
        
    std::uniform_real_distribution<double> distrib(0.0, 1.0);

    std::vector<AbCycle> e_set;

    for (const AbCycle &cycle : cycles) {
        if (distrib(rng) < inclusion_prob) {
            e_set.push_back(cycle);
        }
    }

    return e_set;
}

// Builds measurements (weights) for each cycle based on their relationships
AbCycleWeights build_ab_cycle_weights(
    const std::vector<AbCycle> &cycles,
    std::size_t num_cities) {
    
    AbCycleWeights weights;

    std::size_t num_cycles = cycles.size();

    weights.shared_cities_total.assign(num_cycles, 0);
    weights.shared_cities_between.assign(num_cycles, std::vector<int>(num_cycles, 0));

    std::vector<std::array<int, 2>> owning_cycle(num_cities, {-1, -1});

    for (int i = 0; i < num_cycles; i++) {
        for (const TaggedEdge &te : cycles[i]) {

            if (te.parent != Parent::A) {
                continue;
            }

            for (int city : {te.edge.from, te.edge.to}) {
                if (owning_cycle[city][0] == -1) {
                    owning_cycle[city][0] = i;
                }

                else if (owning_cycle[city][1] == -1) {
                    owning_cycle[city][1] = i;
                }
            }
        }
    }

    for (int city = 0; city < num_cities; city++) {
        int cycle_a = owning_cycle[city][0];
        int cycle_b = owning_cycle[city][1];
        
        if (cycle_a == -1 || cycle_b == -1 || cycle_a == cycle_b) {
            continue;
        }

        weights.shared_cities_total[cycle_a]++;
        weights.shared_cities_total[cycle_b]++;

        weights.shared_cities_between[cycle_a][cycle_b]++;
        weights.shared_cities_between[cycle_b][cycle_a]++;
    }

    return weights;
}

// Minimizes number of conflicting cities in the E-set
// by iteratively adding/removing cycles
std::vector<int> improve_e_set(
    int anchor_cycle_idx,
    const std::vector<int> &initial_cycles,
    const std::vector<int> &shared_cities_total,
    const std::vector<std::vector<int>> &shared_cities_between,
    const std::vector<int> &cycle_half_edge_count,
    std::mt19937 &rng,
    const int max_consecutive_non_improving_iter_count
) {

    const std::size_t num_cycles = shared_cities_total.size();

    // isUsed[i]: whether cycle "i" is currently in the E-set
    std::vector<bool> is_used(num_cycles, false);

    int conflicting_cities_count = 0;

    // For cycle "i": total number of boundary-cities shared with 
    // every currently-selected/used cycle in the E-set summed together
    // shared_cities_with_selected[i] =
    // sum over each selected (used in the E-set) cycle "s" of shared_cities_between[i][s]
    std::vector<int> shared_cities_with_selected(num_cycles, 0);

    // Adds cycle "addedIdx" to the E-set and updates its dependent states, "remove_cycle" exact inverse
    auto add_cycle = [&](int added_idx) {
        is_used[added_idx] = true;

        conflicting_cities_count += shared_cities_total[added_idx] - 2*shared_cities_with_selected[added_idx];

        for (int i = 0; i < num_cycles; i++) {
            shared_cities_with_selected[i] += shared_cities_between[i][added_idx];
        }
    };

    // Removes cycle "removedIdx" from the E-set and updates its dependent states, "add_cycle" exact inverse
    auto remove_cycle = [&](int removed_idx) {
        is_used[removed_idx] = false;

        conflicting_cities_count -= shared_cities_total[removed_idx] - 2*shared_cities_with_selected[removed_idx];

        for (int i = 0; i < num_cycles; i++) {
            shared_cities_with_selected[i] -= shared_cities_between[i][removed_idx];
        }
    };

    // Build later-improved, initial E-set
    for (int idx : initial_cycles) {
        add_cycle(idx);
    }

    std::vector<bool> best_is_used = is_used;
    int consecutive_non_improving_iter_count = 0;
    int best_conflicting_cities_count = conflicting_cities_count;  // The lower - the better

    while (consecutive_non_improving_iter_count < max_consecutive_non_improving_iter_count) {

        // Valid cycle with smallest "delta"
        int best_cand_idx = -1;

        int best_delta = std::numeric_limits<int>::max();

        auto consider_cand = [&](int idx, int delta) {
            if (delta < best_delta) {
                best_delta = delta;
                best_cand_idx = idx;
            }
        };

        for (int i = 0; i < num_cycles; i++) {

            if (i == anchor_cycle_idx) {
                continue;
            }

            // How much the conflicting city count would change if cycle flipped 
            // (added if currently unselected, removed if currently selected)
            int delta;
            
            if (!is_used[i] && shared_cities_with_selected[i] > 0) {
                delta = shared_cities_total[i] - 2*shared_cities_with_selected[i];
                consider_cand(i, delta);
            }

            else if (is_used[i]) {
                delta = -(shared_cities_total[i] - 2*shared_cities_with_selected[i]);
                consider_cand(i, delta);
            }

            // Not a valid candidate
            else {
                continue;
            }
        }

        // Apply move which scan found best
        // No valid candidate found, no move to apply - only anchor cycle is left in the E-set
        if (best_cand_idx != -1) {
            if (is_used[best_cand_idx]) {
                remove_cycle(best_cand_idx);
            }
            
            else {
                add_cycle(best_cand_idx);
            }
        }

        // Check if iteration set new record for lowest (best) conflictingCitiesCount
        if (conflicting_cities_count < best_conflicting_cities_count) {
            consecutive_non_improving_iter_count = 0;
            best_conflicting_cities_count = conflicting_cities_count;
            best_is_used = is_used;
        }

        else {
            consecutive_non_improving_iter_count++;
        }
    }

    // Convert used-cycle boolean array to best-cycle-index array to return
    std::vector<int> best_indices;
    for (int i = 0; i < num_cycles; i++) {
        if (best_is_used[i]) {
            best_indices.push_back(i);
        }
    }

    return best_indices;
}

// Gets the half the number of edges for each cycle
std::vector<int> get_cycle_half_edge_counts(const std::vector<AbCycle> &cycles) {
    std::vector<int> cycle_half_edge_counts;
    cycle_half_edge_counts.reserve(cycles.size());

    for (const AbCycle &cycle : cycles) {
        cycle_half_edge_counts.push_back(static_cast<int>(cycle.size()) / 2);
    }

    return cycle_half_edge_counts;
}

// Selects a subset (E-set) of AB-cycles to form the E-set for the crossover operation
// Starts from a random anchor cycle and adds cycles build around it to build an initial E-set,
// then improves it by iteratively adding/removing cycles
// to minimize the number of conflicting cities in the E-set ("improve_e_set" function)
std::vector<AbCycle> select_e_set(
    const std::vector<AbCycle> &cycles,
    const AbCycleWeights &weights,
    const std::vector<int> cycle_half_edge_counts,
    std::mt19937 &rng
) {

    if (cycles.empty()) {
        return {};
    }

    std::uniform_int_distribution<int> distrib(0, static_cast<int>(cycles.size() - 1));
    int anchor_cycle_idx = distrib(rng);

    std::vector<int> initial_cycles = {anchor_cycle_idx};

    std::uniform_int_distribution<int> coin_flip(0, 1);

    for (std::size_t i = 0, N = cycles.size(); i < N; i++) {

        // Skip already added anchor cycle
        if (anchor_cycle_idx == i) {
            continue;
        }

        bool shares_city_with_anchor =
            weights.shared_cities_between[anchor_cycle_idx][i] > 0;  // "Touches" anchor cycle
        bool is_smaller_than_anchor =
            cycle_half_edge_counts[anchor_cycle_idx] > cycle_half_edge_counts[i];

        if (shares_city_with_anchor && is_smaller_than_anchor
            && coin_flip(rng) == 0) {
            initial_cycles.push_back(i);
        }
    }

    std::vector<int> best_indices = improve_e_set(
        anchor_cycle_idx, initial_cycles,
        weights.shared_cities_total, weights.shared_cities_between,
        cycle_half_edge_counts, rng);

    std::vector<AbCycle> e_set;
    for (int idx : best_indices) {
        e_set.push_back(cycles[idx]);
    }

    return e_set;
}

}

}