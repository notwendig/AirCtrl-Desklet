/**
 * @file control_values.hpp
 * @brief Shared validation for device control requests.
 */
#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>

namespace airctrl {

/**
 * @brief Validate one non-empty AC2729 control request.
 * @return An empty string on success, otherwise a German error message.
 */
std::string controlValuesError(const nlohmann::json& values);

} // namespace airctrl
