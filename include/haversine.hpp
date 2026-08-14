#pragma once

#include <cmath>
#include <numbers>

// Returns (air) distance in km between 2 points using Haversine formula
// Note: < ~0.5% error (typically less), not worth using more precise formulas (e.g., Vincenty's)
inline double haversine_dist(double lat1, double lng1, double lat2, double lng2) {
    // Convert coordinates (in degrees) to radians
    auto degrees_to_radians = [](double deg) { return deg * std::numbers::pi / 180.0; };

    lat1 = degrees_to_radians(lat1);
    lng1 = degrees_to_radians(lng1);
    lat2 = degrees_to_radians(lat2);
    lng2 = degrees_to_radians(lng2);

    // Differences in latitude/longitude between the 2 points (radians)
    double delta_lat = lat2 - lat1;
    double delta_lng = lng2 - lng1;

    // Compute haversine of the central angle in order to compute angular distance ("c") later
    double a = std::pow(std::sin(delta_lat / 2.0), 2)
        + std::cos(lat1) * std::cos(lat2) * std::pow(std::sin(delta_lng / 2.0), 2);

    // Compute angular distance (radians)
    double c = 2 * std::atan2(std::sqrt(a), std::sqrt(1 - a));

    constexpr int EARTH_RADIUS_KM = 6371;
    return EARTH_RADIUS_KM * c;  // Air distance (km)
}