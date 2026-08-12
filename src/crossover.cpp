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

// Performs Edge Assembly Crossover (EAX) between 2 parent tours
// and returns the resulting offspring
Tour EAX::crossover(const Tour &parentA, const Tour &parentB) {

    // STEP 1: build temporary AB-graph to produce AB-cycles (used in STEP 2 choose the E-set)

    std::vector<Edge> edgesA = getEdges(parentA);
    std::vector<Edge> edgesB = getEdges(parentB);

    TaggedEdges taggedEdges = tagEdgesWithParent(edgesA, edgesB);

    ABGraph graph = buildAdjGraph(taggedEdges, parentA.size());

    std::vector<ABCycle> cycles = getABCycles(graph, taggedEdges);

    // STEP 2: select a subset (E-set) of AB-cycles to form the E-set for the crossover operation

    ABCycleWeights weights = buildABCycleWeights(cycles, parentA.size());
    
    std::vector<int> cycleHalfEdgeCount = getCycleHalfEdgeCounts(cycles);

    std::random_device rd;
    std::mt19937 g(rd());

    std::vector<EAX::ABCycle> eSet = selectESet(cycles, weights, cycleHalfEdgeCount, g);
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
// so that (t, z) and (z, t) are treated as identical
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

// Selects a subset (E-set) of AB-cycles randomly
std::vector<EAX::ABCycle> EAX::selectESetRand(
    const std::vector<EAX::ABCycle> &cycles,
    std::mt19937 &g,
    double inclusionProb) {
        
     std::uniform_real_distribution<double> distribution(0.0, 1.0);

     std::vector<EAX::ABCycle> eSet;

     for (const ABCycle &cycle : cycles) {
        if (distribution(g) < inclusionProb) {
            eSet.push_back(cycle);
        }
     }

     return eSet;
}

// Builds measurements (weights) for each cycle based on their relationships
EAX::ABCycleWeights EAX::buildABCycleWeights(
    const std::vector<EAX::ABCycle> &cycles,
    int numCities) {
    
    EAX::ABCycleWeights weights;

    int numCycles = static_cast<int>(cycles.size());

    weights.sharedCitiesTotal.assign(numCycles, 0);
    weights.sharedCitiesBetween.assign(numCycles, std::vector<int>(numCycles, 0));

    std::vector<std::array<int, 2>> owningCycle(numCities, {-1, -1});

    for (int cycleIdx = 0; cycleIdx < numCycles; cycleIdx++) {
        for (const EAX::TaggedEdge &te : cycles[cycleIdx]) {

            if (te.parent != Parent::A) {
                continue;
            }

            for (int city : {te.edge.from, te.edge.to}) {
                if (owningCycle[city][0] == -1) {
                    owningCycle[city][0] = cycleIdx;
                }

                else if (owningCycle[city][1] == -1) {
                    owningCycle[city][1] = cycleIdx;
                }
            }
        }
    }

    for (int city = 0; city < numCities; city++) {
        int cycleA = owningCycle[city][0];
        int cycleB = owningCycle[city][1];
        
        if (cycleA == -1 || cycleB == -1 || cycleA == cycleB) {
            continue;
        }

        weights.sharedCitiesTotal[cycleA]++;
        weights.sharedCitiesTotal[cycleB]++;

        weights.sharedCitiesBetween[cycleA][cycleB]++;
        weights.sharedCitiesBetween[cycleB][cycleA]++;
    }

    return weights;
}

// Minimizes number of conflicting cities in the E-set by iteratively adding/removing cycles
std::vector<int> EAX::improveESet(
    int anchorCycleIdx,
    const std::vector<int> &initialCycles,
    const std::vector<int> &sharedCitiesTotal,
    const std::vector<std::vector<int>> &sharedCitiesBetween,
    const std::vector<int> &cycleHalfEdgeCount,
    std::mt19937 &g,
    const int maxConsecutiveNonImprovingIterCount = 20
) {

    const int cycleCount = static_cast<int>(sharedCitiesTotal.size());

    // isUsed[i]: whether cycle "i" is currently in the E-set
    std::vector<bool> isUsed(cycleCount, false);

    int conflictingCitiesCount = 0;

    // For cycle "i": total number of boundary-cities shared with 
    // every currently-selected/used cycle in the E-set summed together
    // sharedCitiesWithSelected[i] = sum over each selected (used in the E-set) cycle "s" of sharedCitiesBetween[i][s]
    std::vector<int> sharedCitiesWithSelected(cycleCount, 0);

    // Adds cycle "addedIdx" to the E-set and updates its dependent states, "removeCycle" exact inverse
    auto addCycle = [&](int addedIdx) {
        isUsed[addedIdx] = true;

        conflictingCitiesCount += sharedCitiesTotal[addedIdx] - 2*sharedCitiesWithSelected[addedIdx];

        for (int i = 0; i < cycleCount; i++) {
            sharedCitiesWithSelected[i] += sharedCitiesBetween[i][addedIdx];
        }
    };

    // Removes cycle "removedIdx" from the E-set and updates its dependent states, "addCycle" exact inverse
    auto removeCycle = [&](int removedIdx) {
        isUsed[removedIdx] = false;

        conflictingCitiesCount -= sharedCitiesTotal[removedIdx] - 2*sharedCitiesWithSelected[removedIdx];

        for (int i = 0; i < cycleCount; i++) {
            sharedCitiesWithSelected[i] -= sharedCitiesBetween[i][removedIdx];
        }
    };

    // Build later-improved, initial E-set
    for (int idx : initialCycles) {
        addCycle(idx);
    }

    std::vector<bool> bestIsUsed = isUsed;
    int consecutiveNonImprovingIterCount = 0;
    int bestConflictingCitiesCount = conflictingCitiesCount;

    while (consecutiveNonImprovingIterCount < maxConsecutiveNonImprovingIterCount) {

        // Valid cycle with smallest "delta"
        int bestCandIdx = -1;

        int bestDelta = std::numeric_limits<int>::max();

        auto considerCand = [&](int idx, int delta) {
            if (delta < bestDelta) {
                bestDelta = delta;
                bestCandIdx = idx;
            }
        };

        for (int i = 0; i < cycleCount; i++) {

            if (i == anchorCycleIdx) {
                continue;
            }

            // How much the conflicting city count would change if cycle flipped 
            // (added if currently unselected, removed if currently selected)
            int delta;
            
            if (!isUsed[i] && sharedCitiesWithSelected[i] > 0) {
                delta = sharedCitiesTotal[i] - 2*sharedCitiesWithSelected[i];
                considerCand(i, delta);
            }

            else if (isUsed[i]) {
                delta = -(sharedCitiesTotal[i] - 2*sharedCitiesWithSelected[i]);
                considerCand(i, delta);
            }

            // Not a valid candidate
            else {
                continue;
            }
        }

        // Apply move which scan found best
        if (bestCandIdx != -1) {  // No valid candidate found, no move to apply - only anchor cycle is left in the E-set
            if (isUsed[bestCandIdx]) {
                removeCycle(bestCandIdx);
            }
            
            else {
                addCycle(bestCandIdx);
            }
        }

        // Check if iteration set new record for lowest (best) conflictingCitiesCount
        if (conflictingCitiesCount < bestConflictingCitiesCount) {
            consecutiveNonImprovingIterCount = 0;
            bestConflictingCitiesCount = conflictingCitiesCount;
            bestIsUsed = isUsed;
        }

        else {
            consecutiveNonImprovingIterCount++;
        }
    }

    // Convert used-cycle boolean array to best-cycle-index array to return
    std::vector<int> bestIndices;
    for (int i = 0; i < cycleCount; i++) {
        if (bestIsUsed[i]) {
            bestIndices.push_back(i);
        }
    }

    return bestIndices;
}

// Gets the half the number of edges for each cycle
std::vector<int> EAX::getCycleHalfEdgeCounts(const std::vector<EAX::ABCycle> &cycles) {
    std::vector<int> cycleHalfEdgeCount;
    cycleHalfEdgeCount.reserve(cycles.size());

    for (const EAX::ABCycle &cycle : cycles) {
        cycleHalfEdgeCount.push_back(static_cast<int>(cycle.size()) / 2);
    }

    return cycleHalfEdgeCount;
}

// Selects a subset (E-set) of AB-cycles to form the E-set for the crossover operation
// Starts from a random anchor cycle and adds cycles build around it to build an initial E-set,
// then improves it by iteratively adding/removing cycles
// to minimize the number of conflicting cities in the E-set ("improveESet" function)
std::vector<EAX::ABCycle> EAX::selectESet(
    const std::vector<EAX::ABCycle> &cycles,
    const EAX::ABCycleWeights &weights,
    std::vector<int> cycleHalfEdgeCount,
    std::mt19937 &g
) {

    if (cycles.empty()) {
        return {};
    }

    std::uniform_int_distribution<int> distribution(0, static_cast<int>(cycles.size() - 1));
    int anchorCycleIdx = distribution(g);

    std::vector<int> initialCycles = {anchorCycleIdx};

    std::uniform_int_distribution<int> coinFlip(0, 1);

    for (int i = 0; i < static_cast<int>(cycles.size()); i++) {

        // Skip already added anchor cycle
        if (anchorCycleIdx == i) {
            continue;
        }

        bool sharesCityWithAnchor = weights.sharedCitiesBetween[anchorCycleIdx][i] > 0;  // "Touches" anchor cycle
        bool isSmallerThanAnchor = cycleHalfEdgeCount[anchorCycleIdx] > cycleHalfEdgeCount[i];

        if (sharesCityWithAnchor && isSmallerThanAnchor
            && coinFlip(g) == 0) {
            initialCycles.push_back(i);
        }
    }

    std::vector<int> bestIndices = EAX::improveESet(
        anchorCycleIdx, initialCycles,
        weights.sharedCitiesTotal, weights.sharedCitiesBetween,
        cycleHalfEdgeCount, g);

    std::vector<EAX::ABCycle> eSet;
    for (int idx : bestIndices) {
        eSet.push_back(cycles[idx]);
    }

    return eSet;
}