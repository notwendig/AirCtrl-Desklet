/**
 * @file main.cpp
 * @brief Command-line entry point for airctrl-server.
 */
#include "airctrl_version.hpp"
#include "server.h"

#include <iostream>
#include <string>
#include <utility>

namespace {

void printHelp() {
    std::cout << "AirControl-TCP-Server; einziger Prozess mit AC2729-Zugriff\n\n"
              << "Aufruf: airctrl-server [Optionen]\n"
              << "  -h, --help           Diese Hilfe anzeigen\n"
              << "  -v, --version        Version anzeigen\n"
              << "      --config DATEI   Serverkonfiguration (Standard: /etc/airctrld.cfg)\n"
              << "      --check-config   Konfiguration prüfen und beenden\n";
}

} // namespace

int main(int argc, char** argv) {
    std::string configPath = "/etc/airctrld.cfg";
    bool checkConfig = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "-h" || argument == "--help") {
            printHelp();
            return 0;
        }
        if (argument == "-v" || argument == "--version") {
            std::cout << "airctrl-server " << AIRCTRL_VERSION << '\n';
            return 0;
        }
        if (argument == "--check-config") {
            checkConfig = true;
            continue;
        }
        if (argument == "--config" && index + 1 < argc) {
            configPath = argv[++index];
            continue;
        }
        if (argument.rfind("--config=", 0U) == 0U) {
            configPath = argument.substr(std::string("--config=").size());
            continue;
        }
        std::cerr << "Unbekannte Option: " << argument << '\n';
        return 2;
    }
    return airctrl::runServer(std::move(configPath), checkConfig);
}
