/**
 * @file statuslog.h
 * @brief Append-only CSV logging for confirmed device status objects.
 */
#pragma once

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace airctrl {

/**
 * Writes one RFC-4180-compatible line for every valid status object.
 *
 * The first status fixes the sorted set of regular columns. Fields introduced
 * later are retained in the reserved _extra_json column, keeping old rows and
 * plotting scripts usable across firmware changes.
 */
class StatusCsvLog final {
public:
    explicit StatusCsvLog(std::string path);

    bool initialize(std::string* error);
    bool append(const nlohmann::json& status, std::string* error);

private:
    bool loadHeader(std::string* error);
    bool writeRecord(const std::string& record, std::string* error) const;

    std::string path_;
    std::vector<std::string> columns_;
};

} // namespace airctrl
