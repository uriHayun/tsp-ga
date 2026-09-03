#pragma once

#include "city.hpp"
#include "edge.hpp"

#include <cmath>
#include <numbers>

// Returns (air) distance in km between 2 points using Haversine formula
// Note: < ~0.5% error (typically less), not worth using more precise formulas (e.g., Vincenty's)
inline double haversine_distance(const City &city1, const City &city2) {
    // Convert coordinates (in degrees) to radians
    auto degrees_to_radians = [](const double deg) { return deg * std::numbers::pi / 180.0; };

    const double lat1 = degrees_to_radians(city1.lat);
    const double lng1 = degrees_to_radians(city1.lng);
    const double lat2 = degrees_to_radians(city2.lat);
    const double lng2 = degrees_to_radians(city2.lng);

    // Differences in latitude/longitude between the 2 points (radians)
    const double delta_lat = lat2 - lat1;
    const double delta_lng = lng2 - lng1;

    // Compute haversine of the central angle in order to compute angular distance ("c") later
    const double a = std::pow(std::sin(delta_lat / 2.0), 2)
        + std::cos(lat1) * std::cos(lat2) * std::pow(std::sin(delta_lng / 2.0), 2);

    // Compute angular distance (radians)
    const double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));

    constexpr double EARTH_RADIUS_KM = 6371.0;
    return EARTH_RADIUS_KM * c;  // Air distance (km)
}

// Wrapper function for computing distance between the edge's 2 cities
// using "haversine_distance"
inline double edge_len(const Edge &edge, const Cities &cities) {
    const City &from = cities[edge.from];
    const City &to = cities[edge.to];

    return haversine_distance(from, to);
}