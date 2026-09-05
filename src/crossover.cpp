#include "city.hpp"
#include "crossover.hpp"
#include "distance.hpp"
#include "edge.hpp"
#include "tour.hpp"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstddef>
#include <functional>
#include <limits>
#include <random>
#include <tuple>
#include <utility>
#include <vector>

namespace Eax::Detail {

// Hash function for using an Edge in an unordered_set
std::size_t EdgeHash::operator()(const EdgeKey &key) const noexcept {
    const std::size_t h1 = std::hash<std::size_t>{}(key.first);
    const std::size_t h2 = std::hash<std::size_t>{}(key.second);

    return h1 ^ (h2 << 1);
}

// Extracts all edges from a tour, including the closing edge 
// from the last city to the first using modulo the tour length
// e.g., { 1, 4, 2, 3 } to { (1, 4), (4, 2), (2, 3), (3, 1) }
Edges get_edges(const Tour &tour) {

    Edges edges;
    edges.reserve(tour.size());

    for (std::size_t from_idx = 0, N = tour.size(); from_idx < N; from_idx++) {
        std::size_t to_idx = (from_idx + 1) % N;

        edges.push_back({ tour[from_idx], tour[to_idx] });
    }

    return edges;
}

// Returns a standard representation of an edge
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
// 2 TaggedEdges are considered equal if they:
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
EdgeSet build_edge_set(const Edges &edges) {
    EdgeSet edge_set;

    for (const Edge &edge : edges) {
        edge_set.insert(normalize_edge(edge));
    }

    return edge_set;
}

// Returns edges appear in "src" which do not appear in "other"
Edges get_unique_edges(
    const Edges &src,
    const Edges &other) {

        EdgeSet other_set = build_edge_set(other);

        Edges unique_edges;
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
    const Edges &edges_a,
    const Edges &edges_b) {

        TaggedEdges edges;
        edges.reserve(edges_a.size() + edges_b.size());

        for (const Edge &edge : edges_a) {
            edges.push_back({edge, Parent::A});
        }

        for (const Edge &edge : edges_b) {
            edges.push_back({ edge, Parent::B });
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
    const TaggedEdges &edges,
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
std::size_t get_other_endpoint(const TaggedEdge &te, std::size_t curr_city) {
    return (te.edge.from == curr_city)
        ? te.edge.to
        : te.edge.from;
}

// Builds the initial collection of unused edges,
// each edge being tracked once rather than once per endpoint:
// (if edge (t, z) exists, there's no need for edge (z, t))
TaggedEdgeSet build_unused_edges_set(const TaggedEdges &edges) {
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
AbCycles build_ab_cycles(
    const AbGraph &graph,
    const TaggedEdges &edges) {

    AbCycles cycles;

    TaggedEdgeSet unused_edges = build_unused_edges_set(edges);

    while (!unused_edges.empty()) {
        auto it = unused_edges.begin();
        TaggedEdge start_edge = *it;
        
        std::size_t start_city = start_edge.edge.from;

        AbCycle curr_cycle;

        TaggedEdge curr_edge = start_edge;
        std::size_t curr_city = curr_edge.edge.from;

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
ESet select_e_set_rand(
    const AbCycles &cycles,
    std::mt19937_64 &rng,
    const double INCLUSION_PROB) {
        
    std::uniform_real_distribution<double> distrib(0.0, 1.0);

    ESet e_set;

    for (const AbCycle &cycle : cycles) {
        if (distrib(rng) < INCLUSION_PROB) {
            e_set.push_back(cycle);
        }
    }

    return e_set;
}

// Builds measurements (weights) for each cycle based on their relationships
AbCycleWeights build_ab_cycle_weights(
    const AbCycles &cycles,
    const std::size_t num_cities) {
    
    AbCycleWeights weights;

    const std::size_t num_cycles = cycles.size();

    weights.shared_cities_total.assign(num_cycles, 0);
    weights.shared_cities_between.assign(num_cycles, std::vector<int>(num_cycles, 0));

    std::vector<std::array<int, 2>> owning_cycle(num_cities, {-1, -1});

    for (std::size_t i = 0; i < num_cycles; i++) {
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

    for (std::size_t city = 0; city < num_cities; city++) {
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
    std::size_t anchor_cycle_idx,
    const std::vector<std::size_t> &initial_cycles_idxs,
    const std::vector<int> &shared_cities_total,
    const std::vector<std::vector<int>> &shared_cities_between,
    const std::vector<int> &cycle_half_edge_count,
    std::mt19937_64 &rng,
    const int MAX_CONSECUTIVE_NON_IMPROVING_ITERS_COUNT,
    const int MAX_FROZEN_ITERS) {

    const std::size_t num_cycles = shared_cities_total.size();

    // "Freeze" cycle when flipped for 10 iterations, preventing it from being immediately flipped back over again
    // (cycles flipped had the highest score among the other cycles,
    // this prevents then from naturally getting reverse-flipped fcountless times)
    std::vector<int> freeze_iters_left(num_cycles, 0);

    // is_used[i]: whether cycle "i" is currently in the E-set
    std::vector<bool> is_used(num_cycles, false);

    int conflicting_cities_count = 0;

    // For cycle "i": total number of boundary-cities shared with 
    // every currently-selected/used cycle in the E-set summed together
    // shared_cities_with_selected[i] =
    // sum over each selected (used in the E-set) cycle "s" of shared_cities_between[i][s]
    std::vector<int> shared_cities_with_selected(num_cycles, 0);

    // Adds cycle "added_idx" to the E-set and updates its dependent states, "remove_cycle" exact inverse
    auto add_cycle = [&](std::size_t added_idx) {
        is_used[added_idx] = true;

        conflicting_cities_count += shared_cities_total[added_idx] - 2*shared_cities_with_selected[added_idx];

        for (std::size_t i = 0; i < num_cycles; i++) {
            shared_cities_with_selected[i] += shared_cities_between[i][added_idx];
        }
    };

    // Removes cycle "removed_idx" from the E-set and updates its dependent states, "add_cycle" exact inverse
    auto remove_cycle = [&](std::size_t removed_idx) {
        is_used[removed_idx] = false;

        conflicting_cities_count -= shared_cities_total[removed_idx] - 2*shared_cities_with_selected[removed_idx];

        for (std::size_t i = 0; i < num_cycles; i++) {
            shared_cities_with_selected[i] -= shared_cities_between[i][removed_idx];
        }
    };

    // Build later-improved, initial E-set
    for (std::size_t i : initial_cycles_idxs) {
        add_cycle(i);
    }

    std::vector<bool> best_is_used = is_used;
    int consecutive_non_improving_iter_count = 0;
    int best_conflicting_cities_count = conflicting_cities_count;  // The lower - the better

    while (consecutive_non_improving_iter_count < MAX_CONSECUTIVE_NON_IMPROVING_ITERS_COUNT) {

        // Update freeze-iterations passed each iteration for all cycles
        for (int i = 0; i < num_cycles; i++) {
            freeze_iters_left[i]--;
        }

        // Valid cycle with smallest "delta"
        std::size_t best_cand_idx = -1;

        int best_delta = std::numeric_limits<int>::max();

        auto consider_cand = [&](std::size_t idx, int delta) {
            if (delta < best_delta) {
                best_delta = delta;
                best_cand_idx = idx;
            }
        };

        for (int i = 0; i < num_cycles; i++) {

            if (i == anchor_cycle_idx ||
                freeze_iters_left[i] > 0) {
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

            // "Freeze" the just-added cycle so it doesn't get instantly resersed-flipped
            freeze_iters_left[best_cand_idx] = MAX_FROZEN_ITERS;
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

    // Convert used-cycle boolean array to best cycle-index array to return
    std::vector<int> best_indices;
    for (int i = 0; i < num_cycles; i++) {
        if (best_is_used[i]) {
            best_indices.push_back(i);
        }
    }

    return best_indices;
}

// Gets the half the number of edges for each cycle
std::vector<int> get_cycle_half_edge_counts(const AbCycles &cycles) {
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
ESet select_e_set(
    const AbCycles &cycles,
    const AbCycleWeights &weights,
    const std::vector<int> &cycle_half_edge_counts,
    std::mt19937_64 &rng) {

    if (cycles.empty()) {
        return {};
    }

    std::uniform_int_distribution<int> distrib(0, static_cast<int>(cycles.size() - 1));
    std::size_t anchor_cycle_idx = distrib(rng);

    std::vector<size_t> initial_cycles_idxs = { anchor_cycle_idx };

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
            initial_cycles_idxs.push_back(i);
        }
    }

    std::vector<int> best_indices = improve_e_set(
        anchor_cycle_idx, initial_cycles_idxs,
        weights.shared_cities_total, weights.shared_cities_between,
        cycle_half_edge_counts, rng);

    ESet e_set;
    for (std::size_t idx : best_indices) {
        e_set.push_back(cycles[idx]);
    }

    return e_set;
}

// Returns an intermediate, invalid solution represented as an edge-set
// by removing A-edges that are in the E-set,
// and adding B-edges that are in the E-set for each cycle
//
// This breaks the global full lap-around cycle into disconnected loops,
// though keeps every city connected to 2 edges
// (since every removed A-edge is immediately rebalanced by adding the next B-edge in line)
EdgeSet build_initial_offspring_edges(const Edges &edges_a, const ESet &e_set) {
    EdgeSet initial_offspring_edges = build_edge_set(edges_a);

    for (const AbCycle &cycle : e_set) {

        for (const TaggedEdge &te : cycle) {

            if (te.parent == Parent::A) {
                initial_offspring_edges.erase(normalize_edge(te.edge));
            }
            else {
                initial_offspring_edges.insert(normalize_edge(te.edge));
            }
        }
    }

    return initial_offspring_edges;
}

// Decomposes the list of edges ("initial_offspring_edges") into a list of subtours
Subtours decompose_edges_into_subtours(
    const EdgeSet &initial_offspring_edges,
    const std::size_t &num_cities) {

    std::vector<std::vector<std::size_t>> adj(num_cities);

    // Build an adjencency graph to find out which are which neighbors
    // to walk down the list of edges ("initial_offspring_edges")
    for (const EdgeKey &edge : initial_offspring_edges) {
        adj[edge.first].push_back(edge.second);
        adj[edge.second].push_back(edge.first);
    }

    // Keep track of visited cities
    // to know where to start a new "walk" (where a new subtour begins)
    // to find the subtour
    std::vector<bool> is_visited(num_cities, false);

    Subtours subtours;

    // Search for new subtours to retrieve
    for (std::size_t city = 0, N = num_cities; city < N; city++) {

        // An unvisited city marks the start of a new subtour
        if (is_visited[city]) {
            continue;
        }

        // Like we already passed start city to its neighbor
        is_visited[city] = true;
        std::size_t start_city = city;
        std::size_t prev_city = start_city;
        std::size_t curr_city = adj[start_city][0];  // The next city, start city's neighbor

        Edges subtour;

        // Retrieve newly found subtour
        while (start_city != curr_city) {
            subtour.push_back({ prev_city, curr_city });

            is_visited[curr_city] = true;

            std::size_t next_city = (adj[curr_city][0] == prev_city)
                ? adj[curr_city][1]
                : adj[curr_city][0];

            prev_city = curr_city;
            curr_city = next_city;
        }

        subtour.push_back({ prev_city, curr_city });
        subtours.push_back(subtour);
    }

    return subtours;
}

// Returns the index of the subtour with the least number of elements
std::size_t get_shortest_subtour_idx(const Subtours &subtours) {
    std::size_t shortest_subtour_idx = 0;

    for (std::size_t i = 1, N = subtours.size(); i < N; i++) {
        if (subtours[i].size() < subtours[shortest_subtour_idx].size()) {
            shortest_subtour_idx = i;
        }
    }

    return shortest_subtour_idx;
}

// Merges all subtours into a single valid type-edges tour, repeatedly taking the shortest subtour,
// and merging it wwhichever other subtour gives the cheapest merge,
// from the merged subtour, cut 2 edges (one from each subtour), and adds 2 (cheapest previously cut edges merge),
// this process resumes until one subtour is left (they reduce by merging together)
Edges merge_subtours_to_tour(Subtours subtours, const Cities &cities) {
    while (subtours.size() > 1) {
        const std::size_t shortest_subtour_idx = get_shortest_subtour_idx(subtours);

        // Smallest found subtours' distance cost-change (link_cost - cut_cost)
        // of cutting edges 1&2, and linking edes 3&4
        double best_cost_change = std::numeric_limits<double>::infinity();

        // The 4 edges which achieve the smallest (best) distance cost-change,
        // edges 1&2 to remove from "subtours", edge3/edge4 to add to the subtours
        Edge best_edge1 = {}, best_edge2 = {}, best_edge3 = {}, best_edge4 = {};

        // Index of the subtour containing the winning edge2,
        // subtours[best_cand_idx] to be merged with subtours[shortest_subtour_idx]
        // once the search completes
        std::size_t best_cand_idx = 0;

        for (std::size_t i = 0, N = subtours.size(); i < N; i++) {
            if (i == shortest_subtour_idx) {
                continue;
            }

            // 1st edge (e) - derived from: each edge over the shortest subtour
            for (const Edge &edge1 : subtours[shortest_subtour_idx]) {
                // 2nd edge (e') - derived from: each edge over other subtours
                // (excluding shortest subtour)
                for (const Edge &edge2 : subtours[i]) {

                    // 3rd, 4th edges (e'', e''') - constructed from: length-wise cheapest edges 1&2 merge

                    const Edge edge3_a = { edge1.from, edge2.from };
                    const Edge edge4_a = { edge1.to, edge2.to };

                    const Edge edge3_b = { edge1.from, edge2.to };
                    const Edge edge4_b = { edge1.to, edge2.from };

                    // Compute edges 3&4's combined distance cost for later comparasion
                    const double link_cost1 = edge_len(edge3_a, cities) + edge_len(edge4_a, cities);
                    const double link_cost2 = edge_len(edge3_b, cities) + edge_len(edge4_b, cities);

                    // Smallest distance cost of edges 3&4 to be linked to "subtours"
                    double link_cost;

                    Edge edge3, edge4;

                    // Choose edges 3&4 with length-wise cheapest cost,
                    // and keep their combined distance cost
                    if (link_cost1 < link_cost2) {
                        link_cost = link_cost1;

                        edge3 = edge3_a;
                        edge4 = edge4_a;
                    } 
                    else {
                        link_cost = link_cost2;

                        edge3 = edge3_b;
                        edge4 = edge4_b;
                    }

                    // Smallest distance cost of edges 1&2 to be cut from "subtours"
                    const double cut_cost = edge_len(edge1, cities) + edge_len(edge2, cities);
                    
                    const double cost_change = link_cost - cut_cost;

                    if (cost_change < best_cost_change) {
                        best_cost_change = cost_change;

                        best_edge1 = edge1;
                        best_edge2 = edge2;
                        best_edge3 = edge3;
                        best_edge4 = edge4;

                        best_cand_idx = i;
                    }
                }
            }
        }

        // Build type "Edges" offspring, later turned into a type "Tour" offspring
        Edges offspring_edges = subtours[shortest_subtour_idx];
        offspring_edges.reserve(subtours[shortest_subtour_idx].size() + subtours[best_cand_idx].size());

        // Merge "subtours[shortest_subtour_idx]", and "subtours[best_cand_idx]"
        offspring_edges.append_range(subtours[best_cand_idx]);

        // Cut edges 1&2 from merged subset
        std::erase_if(offspring_edges, [&](const Edge &edge) {
             return normalize_edge(edge) == normalize_edge(best_edge1) || normalize_edge(edge) == normalize_edge(best_edge2); 
        });

        // Link edges 3&4 to merged subset
        offspring_edges.push_back(best_edge3);
        offspring_edges.push_back(best_edge4);

        // Update "subtours" to reflect the merge: replace one of merged pair with merged result,
        // and remove the other from the pair

        // Overwrite one of merged pair with the merged result
        subtours[shortest_subtour_idx] = offspring_edges;

        // Remove the other subtour from the merged pair
        // (subtours' order doesn't matter - so we're able to swap end subtour with wanted subtour to erase,
        // and erase end-element in O(1), this avoid std::vector::erase's iterative O(n) slow approach)
        std::swap(subtours[best_cand_idx], subtours.back());
        subtours.pop_back();
    }

    // At this point, "subtours" only contains one final element - the type "Edges" tour
    return subtours[0];
}

// Turn type-edges tour to a regular tour to return
// e.g., { {1, 2}, {2, 4}, {4, 3}, {3, 1} } to { 1, 2, 4, 3 }
Tour edges_to_tour(Edges tour_edges, const std::size_t &num_cities) {
    std::vector<std::vector<std::size_t>> adj(num_cities);

    // Build an adjencency graph to find out which are which neighbors
    // to walk down "tour_edges"
    for (const Edge &edge : tour_edges) {
        adj[edge.from].push_back(edge.to);
        adj[edge.to].push_back(edge.from);
    }

    std::size_t start_city = tour_edges[0].from;
    std::size_t prev_city = start_city;
    std::size_t curr_city = adj[start_city][0];  // The next city, start city's neighbor

    Tour tour;

    tour.push_back(start_city);

    // Build tour until touch "start_city" in the end-edge
    while (start_city != curr_city) {
        tour.push_back(curr_city);

        std::size_t next_city = (adj[curr_city][0] == prev_city)
            ? adj[curr_city][1]
            : adj[curr_city][0];

        prev_city = curr_city;
        curr_city = next_city;
    }

    return tour;
}

}  // Closing namespace Eax::Detail

namespace Eax {

using namespace Detail;

// TODO: comment here
std::tuple<Edges, AbCycles> init_crossover(const Tour &parent_a, const Tour &parent_b) {
    // STEP 1: build temporary AB-graph to produce AB-cycles

    const Edges edges_a = get_edges(parent_a);
    const Edges edges_b = get_edges(parent_b);

    const TaggedEdges tagged_edges = tag_edges_with_parent(edges_a, edges_b);

    const AbGraph graph = build_ab_graph(tagged_edges, parent_a.size());

    // STEP 2: produce AB-cycles from the AB-graph

    const AbCycles cycles = build_ab_cycles(graph, tagged_edges);

    return { edges_a, cycles };
}

// TODO: change comment:
// Performs Edge Assembly Crossover (EAX) between 2 parent tours,
// and returns the resulting offspring (child)
Tour crossover(const AbCycles &cycles, const Edges &edges_a, const Cities cities) {

    // STEP 3: select a subset (E-set) of AB-cycles to form the E-set for the crossover operation

    const AbCycleWeights weights = build_ab_cycle_weights(cycles, cities.size());
    
    const std::vector<int> cycle_half_edge_counts = get_cycle_half_edge_counts(cycles);

    std::random_device rd;
    std::mt19937_64 rng(rd());

    const ESet e_set = select_e_set(cycles, weights, cycle_half_edge_counts, rng);

    // STEP 4: Generate an intermediate solution from parent-A by removing the edges of E-set's A-edges and
    //         adding the edges of E-set's B-edges

    const EdgeSet initial_offspring_edges = build_initial_offspring_edges(edges_a, e_set);

    // STEP 5: connect all sub-tours into a tour to generate a valid offspring

    const Subtours subtours = decompose_edges_into_subtours(initial_offspring_edges,
        cities.size());

    const Edges offspring_edges = merge_subtours_to_tour(subtours, cities);

    Tour offspring = edges_to_tour(offspring_edges, cities.size());

    return offspring;
}

}  // Closing namespace Eax