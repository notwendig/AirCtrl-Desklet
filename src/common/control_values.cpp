/**
 * @file control_values.cpp
 * @brief Shared validation for device control requests.
 */
#include "control_values.hpp"

#include <nlohmann/json.hpp>

namespace airctrl {

std::string controlValuesError(const nlohmann::json& values) {
    if (!values.is_object() || values.empty()) return "Leerer Steuerauftrag.";
    const bool numbers = values.begin().value().is_number();
    for (nlohmann::json::const_iterator item = values.begin(); item != values.end(); ++item) {
        const nlohmann::json& value = item.value();
        const std::string& key = item.key();
        const bool isNumber = value.is_number();
        double number = -1.0;
        if (isNumber) number = value.get<double>();
        const bool valid =
            (key == "pwr" && value.is_string() && (value == "0" || value == "1")) ||
            (key == "cl" && value.is_boolean()) ||
            (key == "mode" && value.is_string() &&
             (value == "P" || value == "A" || value == "S" || value == "M")) ||
            (key == "om" && value.is_string() &&
             (value == "1" || value == "2" || value == "3" || value == "s" || value == "t")) ||
            (key == "func" && value.is_string() && (value == "P" || value == "PH")) ||
            (key == "uil" && value.is_string() && (value == "0" || value == "1")) ||
            (key == "rhset" && isNumber &&
             (number == 40.0 || number == 50.0 || number == 60.0 || number == 70.0)) ||
            (key == "aqil" && isNumber &&
             (number == 0.0 || number == 25.0 || number == 50.0 || number == 75.0 || number == 100.0)) ||
            (key == "dt" && isNumber && number >= 0.0 && number <= 12.0 &&
             number == static_cast<double>(static_cast<int>(number)));
        if (!valid) return "Ungültiger Steuerwert: " + key;
        if (isNumber != numbers)
            return "Ein Steuerauftrag darf Ganzzahlen nicht mit Text- oder Boolean-Werten mischen.";
    }
    return {};
}

} // namespace airctrl
