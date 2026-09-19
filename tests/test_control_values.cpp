#include "control_values.hpp"

#include <nlohmann/json.hpp>

#include <cassert>

int main() {
    using airctrl::controlValuesError;
    using Json = nlohmann::json;

    assert(controlValuesError(Json::object()) == "Leerer Steuerauftrag.");
    assert(controlValuesError({{"pwr","1"}}).empty());
    assert(controlValuesError({{"mode","S"},{"om","s"},{"uil","0"}}).empty());
    assert(controlValuesError({{"rhset",50},{"dt",12}}).empty());
    assert(controlValuesError({{"cl",true}}).empty());

    assert(!controlValuesError({{"pwr","2"}}).empty());
    assert(!controlValuesError({{"dt",1.5}}).empty());
    assert(!controlValuesError({{"mode","P"},{"dt",1}}).empty());
    assert(!controlValuesError({{"unknown","1"}}).empty());
}
