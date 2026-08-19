#include "haversine.hpp"
#include "tour.hpp"

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <curl/curl.h>
#include <fstream>
#include <iostream>
#include <numeric>
#include <nlohmann/json.hpp>
#include <random>
#include <vector>
#include <string>

using json = nlohmann::json;

namespace tsp {

struct City {
    double lat;
    double lng;
};

const Tour &tourney_select(const std::vector<Tour> &pop, const std::vector<City> &cities,
    std::mt19937 &rng, int K = 5);
double fitness(const Tour &tour, const std::vector<City> &cities);
double tour_dist(const Tour &tour, const std::vector<City> &cities);
Tour rand_tour(const int N = 50);
std::string trim(const std::string &value);
std::string read_env_value(const std::string &key);
std::size_t receive_data(void *contents, std::size_t size, std::size_t count, void *userp);

int main() {
    CURL *curl = curl_easy_init();
    if (!curl) {
        // HTTP request failed
        std::cerr << "Failed to initialize CURL\n";
        return 1;
    }

    std::string response;
    
    const std::string GEONAMES_USERNAME = read_env_value("GEONAMES_USERNAME");
    if (GEONAMES_USERNAME.empty()) {
        curl_easy_cleanup(curl);
        std::cerr << "GEONAMES_USERNAME environment variable is not set\n";
        return 1;
    }

    const std::string URL = "https://secure.geonames.org/searchJSON"
        "?country=US&featureClass=P&maxRows=1000&username=" + GEONAMES_USERNAME;

    curl_easy_setopt(curl, CURLOPT_URL, URL.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_data);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    // Follow HTTP redirects (301, 302) automatically
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    // Prevent the request from hanging indefinitely by limiting the connection and total timeouts
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);

    CURLcode result = curl_easy_perform(curl);
    if (result != CURLE_OK) {
        curl_easy_cleanup(curl);
        std::cerr << "Failed to perform CURL request\n";
        return 1;
    }

    long http_status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_status);
    if (http_status != 200) {
        curl_easy_cleanup(curl);
        std::cerr << "HTTP request failed with status code: " << http_status << '\n';
        return 1;
    }

    if (response.empty()) {
        curl_easy_cleanup(curl);
        std::cerr << "Received empty response\n";
        return 1;
    }

    curl_easy_cleanup(curl);

    try {
        json data = json::parse(response);
        if (!data.contains("geonames") || !data["geonames"].is_array()) {
            std::cerr << R"(Invalid response format: "geonames" key not found or is not an array\n)";
            return 1;
        }

        std::vector<City> cities;

        for (const auto &city : data["geonames"]) {
            cities.push_back({
                std::stod(city["lat"].get<std::string>()),
                std::stod(city["lng"].get<std::string>())
            });
        }

        // Print longitude and latitude - just for verification for now
        for (const auto &city : cities) {
            std::cout << "City: lat=" << city.lat << ", lng=" << city.lng << '\n';
        }
    }
    catch (const json::parse_error &e) {
        std::cerr << "Failed to parse JSON response: " << e.what() << '\n';
        return 1;
    } 
    catch (json::exception &e) {
        std::cerr << "JSON exception occurred: " << e.what() << '\n';
        return 1;
    }

    return 0;
}

// Returns a random unsorted sequence of integers 0 to N-1
Tour rand_tour(const int N) {
    // Create a state with N (50) ordered cities (0 to N-1)
    Tour tour(N);
    std::iota(tour.begin(), tour.end(), 0);

    // Shuffle the ordered state to create a random state
    std::random_device rd;
    std::mt19937 rng(rd());

    std::shuffle(tour.begin(), tour.end(), rng);

    return tour;
}

// Returns the total distance of a tour represented by a state
double tour_dist(const Tour &tour, const std::vector<City> &cities) {
    double total_dist = 0.0;

    for (std::size_t i = 0, N = tour.size(); i < N; i++) {
        const City &to_city = cities[tour[i]];
        
        // Wrap around to first using after the last city using modulo operator
        const City &from_city = cities[tour[(i + 1) % tour.size()]];
        total_dist += haversine_dist(from_city.lat, from_city.lng, to_city.lat, to_city.lng);
    }

    return total_dist;
}

// Converts tour's distance into a fitness score, the higher the score the better it is
double fitness(const Tour &tour, const std::vector<City> &cities) {
    double dist = tour_dist(tour, cities);

    // Fitness score is inversely proportional to distance
    // Add a small epsilon to avoid division by 0 (0 distance edge case)
    return 1.0 / (dist + 1e-9);
}

const Tour &tourney_select(
    const std::vector<Tour> &pop, const std::vector<City> &cities,
    std::mt19937 &rng, int K) {
    assert(!pop.empty());

    std::uniform_int_distribution<std::size_t> distrib(0, pop.size() - 1);
    std::size_t best_cand_idx = distrib(rng);
    double best_score = fitness(pop[best_cand_idx], cities);

    for (int i = 0; i < K - 1; i++) {
        const std::size_t cand_idx = distrib(rng);
        const double cand_score = fitness(pop[cand_idx], cities);

        if (best_score < cand_score) {
            // Current candidate becomes the best candidate
            best_cand_idx = cand_idx;
            best_score = cand_score;
        }
    }
    return pop[best_cand_idx];  // Best candidate from tournament
}

// Removes leading and trailing whitespace from a string
std::string trim(const std::string &value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) {
        return "";
    }

    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}


// Reads the value associated with a key from a local .env file
// Must cite AI-generated code: logic for reading from .env file 
// generated by GitHub Copilot (and Claude), modified by dev
std::string read_env_value(const std::string &key) {
    std::ifstream env_file(".env");
    if (!env_file.is_open()) {
        return "";
    }

    std::string line;
    while (std::getline(env_file, line)) {
        if (line.empty() || line[0] == '#') {
            continue;
        }

        const auto separator = line.find('=');
        if (separator == std::string::npos) {
            continue;
        }

        std::string curr_key = trim(line.substr(0, separator));
        std::string curr_value = trim(line.substr(separator + 1));

        if (curr_value.size() >= 2 && curr_value.front() == '"' && curr_value.back() == '"') {
            curr_value = curr_value.substr(1, curr_value.size() - 2);
        }

        if (curr_key == key) {
            return curr_value;
        }
    }

    return "";
}

// libcurl callback to append the HTTP received response data to a string
// Note: libcurl is a C library, so this callback uses raw pointers instead of C++ references
std::size_t receive_data(void *contents, std::size_t size, std::size_t count, void *output_buffer) {
    std::size_t total_size = size * count;
    std::string *res = static_cast<std::string *>(output_buffer);
    res->append(static_cast<char *>(contents),  total_size);

    return total_size;
}

}