#include "crossover.hpp"
#include "tour.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <functional>
#include <random>
#include <vector>

// Performs Edge Assembly Crossover (EAX) between 2 parent tours
// and returns the resulting offspring
Tour EAX::crossover(const Tour &parentA, const Tour &parentB) {

    // STEP 1: build temporary AB-graph to produce AB-cycles (used in STEP 2 choose the E-set)

    std::vector<Edge> edgesA = getEdges(parentA);
    std::vector<Edge> edgesB = getEdges(parentB);

    TaggedEdges taggedEdges = tagEdgesWithParent(edgesA, edgesB);

    ABGraph graph = buildAdjGraph(taggedEdges, parentA.size());

    std::vector<ABCycle> cycles = getABCycles(graph, taggedEdges);
}

// Hash function for using an Edge in an unordered_set
std::size_t EAX::EdgeHash::operator()(const EAX::EdgeKey &key) const noexcept {
    std::size_t h1 = std::hash<int>{}(key.first);
    std::size_t h2 = std::hash<int>{}(key.second);

    return h1 ^ (h2 << 1);
}

// Extracts all edges from a tour, including the closing edge 
// from the last city to the first using modulo the tour length
// e.g., {1, 4, 2, 3} to {(1,4), (4,2), (2,3), (3,1)}
std::vector<EAX::Edge> EAX::getEdges(const Tour &tour) {

    std::vector<EAX::Edge> edges;
    edges.reserve(tour.size());

    for (int fromIdx = 0, N = static_cast<int>(tour.size()); fromIdx < N; fromIdx++) {
        int toIdx = (fromIdx + 1) % N;

        edges.push_back({tour[fromIdx], tour[toIdx]});
    }

    return edges;
}

// Return a canonical representation of an edge
// so that (x, y) nd (y, x) are treated as identical
EAX::EdgeKey EAX::normalizeEdge(const EAX::Edge &edge) {
    return {
        std::min(edge.from, edge.to),
        std::max(edge.from, edge.to)
    };
}

// Hash function for using a TaggedEdge in an unordered_set
std::size_t EAX::TaggedEdgeHash::operator()(const TaggedEdge &te) const noexcept {
    const std::size_t h1 = EAX::EdgeHash{}(EAX::normalizeEdge(te.edge));
    const std::size_t h2 = std::hash<int>{}(static_cast<int>(te.parent));

    return h1 ^ (h2 << 1);
}

// Equality function for using a TaggedEdge in an unordered_set
// 2 TaggedEdges are considered equal if:
// connect the same cities + come from the same parent
bool EAX::TaggedEdgeEqual::operator()(const TaggedEdge &lhs,
                                      const TaggedEdge &rhs) const noexcept {
    return normalizeEdge(lhs.edge) == normalizeEdge(rhs.edge)
           && lhs.parent == rhs.parent;
}

// Returns whether an edge exists in an edge-set using an
// average O(1) hash-table lookup
bool EAX::containsEdge(const EAX::EdgeSet &edgeSet, const EAX::Edge &edge) {
    return edgeSet.contains(EAX::normalizeEdge(edge));
}

// Builds a hash set of edges for fast memory lookups
EAX::EdgeSet EAX::buildEdgeSet(const std::vector<EAX::Edge> &edges) {
    EAX::EdgeSet edgeSet;

    for (const EAX::Edge &edge : edges) {
        edgeSet.insert(EAX::normalizeEdge(edge));
    }

    return edgeSet;
}

// Return edges appear in "src" that do not appear in "other"
std::vector<EAX::Edge> EAX::getUniqueEdges(
    const std::vector<EAX::Edge> &src,
    const std::vector<EAX::Edge> &other) {

        EAX::EdgeSet otherSet = EAX::buildEdgeSet(other);

        std::vector<EAX::Edge> uniqueEdges;
        uniqueEdges.reserve(src.size());

        for (const EAX::Edge &edge : src) {
            if (!containsEdge(otherSet, edge)) {
                uniqueEdges.push_back(edge);
            }
        }

        return uniqueEdges;
    }

// Lables each edge with the parent (A or B) it originated from
EAX::TaggedEdges EAX::tagEdgesWithParent(
    const std::vector<EAX::Edge> &edgesA,
    const std::vector<EAX::Edge> &edgesB) {

        EAX::TaggedEdges edges;
        edges.reserve(edgesA.size() + edgesB.size());

        for (const EAX::Edge &edge : edgesA) {
            edges.push_back({edge, EAX::Parent::A});
        }

        for (const EAX::Edge &edge : edgesB) {
            edges.push_back({edge, EAX::Parent::B});
        }

        return edges;
    }

// Builds the AB adjacency graph used to search for AB-cycles
// Contains just A-only edges or B-only edges, does not contain A and B shared edges
/*
Example AB adjacency graph:

graph[0] =
{
    {{0,1}, Parent::A},
    {{1,4}, Parent::B}
};
*/
EAX::ABGraph EAX::buildAdjGraph(
    const std::vector<EAX::TaggedEdge> &edges,
    int numCities) {
        EAX::ABGraph graph(numCities);

        for (const EAX::TaggedEdge &edge : edges) {
            // Store each edge for both edgepoints
            // so the graph can be traversed from either city
            graph[edge.edge.from].push_back(edge);
            graph[edge.edge.to].push_back(edge);
        }

        return graph;
    }

// Given endpoint of an edge, returns other endpoint
int EAX::getOtherEndpoint(const EAX::TaggedEdge &te, int currCity) {
    return (te.edge.from == currCity)
           ? te.edge.to
           : te.edge.from;
}

// Builds the initial collection of unused edges,
// each edge being tracked once rather than once per endpoint:
// (if edge (t, z) exists, there's no need for edge (z, t))
EAX::TaggedEdgeSet EAX::buildUnusedEdgesSet(const std::vector<EAX::TaggedEdge> &edges) {
    EAX::TaggedEdgeSet unusedEdges;
    unusedEdges.reserve(edges.size());

    for (const EAX::TaggedEdge &edge : edges) {
        unusedEdges.insert(edge);
    }

    return unusedEdges;
}

// Transforms the AB-graph into AB-cycles by repeatedly walking an alternating
// path between A/B edges from an arbitrary edge until returning to the starting
// city
std::vector<EAX::ABCycle> EAX::getABCycles(
    const EAX::ABGraph &graph,
    const EAX::TaggedEdges &edges) {

    std::vector<EAX::ABCycle> cycles;

    TaggedEdgeSet unusedEdges = EAX::buildUnusedEdgesSet(edges);

    while (!unusedEdges.empty()) {
        auto it = unusedEdges.begin();
        EAX::TaggedEdge startEdge = *it;
        
        int startCity = startEdge.edge.from;

        EAX::ABCycle currCycle;

        EAX::TaggedEdge currEdge = startEdge;
        int currCity = currEdge.edge.from;

        while (true) {
            currCycle.push_back(currEdge);
            unusedEdges.erase(currEdge);
            
            currCity = EAX::getOtherEndpoint(currEdge, currCity);
            if (currCity == startCity) {
                break;
            }

            Parent expectedParent = (currEdge.parent == Parent::A)
                                    ? Parent::B
                                    : Parent::A;

            // Track whether an adequate (unused of expected parent from currCity) edge was found
            bool foundNext = false;
            for (const EAX::TaggedEdge &edge : graph[currCity]) {
                if (edge.parent != expectedParent || !unusedEdges.contains(edge)) {
                    continue;
                }

                currEdge = edge;
                foundNext = true;
                break;
            }

            assert(foundNext && "AB-cycle failed to close: dead end before returning to start city");
        }

        cycles.push_back(currCycle);
    }
    
    return cycles;
}