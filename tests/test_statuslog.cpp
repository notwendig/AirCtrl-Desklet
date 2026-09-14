#include "statuslog.h"

#include <fcntl.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace {
bool require(bool condition, const char* message) {
    if (!condition) std::cerr << message << '\n';
    return condition;
}

std::vector<std::string> lines(const std::string& path) {
    std::ifstream stream(path);
    std::vector<std::string> result;
    std::string line;
    while (std::getline(stream, line)) result.push_back(line);
    return result;
}
} // namespace

int main() {
    char path[] = "/tmp/airctrl-statuslog-XXXXXX";
    const int descriptor = mkstemp(path);
    if (!require(descriptor >= 0, "Temporäre Datei konnte nicht erstellt werden.")) return 1;
    close(descriptor);

    bool ok = true;
    std::string error;
    airctrl::StatusCsvLog log(path);
    ok &= require(log.initialize(&error), error.c_str());
    ok &= require(log.append({{"rh", 55}, {"pwr", "1"}, {"cl", true},
                              {"name", "Wohnzimmer, Nord"}}, &error), error.c_str());
    ok &= require(log.append({{"rh", 56}, {"pwr", "1"}, {"cl", false},
                              {"name", "Wohnzimmer\nNord"}, {"new_metric", 7}}, &error), error.c_str());

    std::vector<std::string> content = lines(path);
    ok &= require(content.size() == 3U, "CSV enthält nicht genau Kopfzeile und zwei Datenzeilen.");
    if (content.size() == 3U) {
        ok &= require(content[0] == "timestamp,cl,name,pwr,rh,_extra_json",
                      "CSV-Kopfzeile ist nicht stabil sortiert.");
        ok &= require(content[1].find(",1,\"Wohnzimmer, Nord\",1,55,") != std::string::npos,
                      "Bool-, Text- oder Zahlenwert wurde falsch serialisiert.");
        ok &= require(content[2].find("\"{\"\"new_metric\"\":7}\"") != std::string::npos,
                      "Ein späteres Feld fehlt in _extra_json.");
    }

    // copytruncate-style rotation must be self-healing: the next status restores
    // the header before appending its data row.
    std::ofstream(path, std::ios::trunc).close();
    ok &= require(log.append({{"rh", 57}, {"pwr", "1"}}, &error), error.c_str());
    content = lines(path);
    ok &= require(content.size() == 2U &&
                  content[0] == "timestamp,cl,name,pwr,rh,_extra_json",
                  "Nach einer Kürzung wurde die CSV-Kopfzeile nicht wiederhergestellt.");

    unlink(path);
    return ok ? 0 : 1;
}
