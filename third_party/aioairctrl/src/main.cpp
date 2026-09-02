#include <aioairctrl/client.hpp>
#include <charconv>
#include <csignal>
#include <iostream>
#include <limits>
#include <string_view>
#include <vector>

namespace {
class UsageError : public std::invalid_argument {
public:
    using std::invalid_argument::invalid_argument;
};
volatile std::sig_atomic_t stopped = 0;
void stop_handler(int) { stopped = 1; }
void help() {
    std::cout <<
        "Usage: aioairctrl -H HOST [options] status [-J]\n"
        "       aioairctrl -H HOST [options] status-observe [-J]\n"
        "       aioairctrl -H HOST [options] set [-I] KEY=VALUE [KEY=VALUE ...]\n\n"
        "  -H, --host HOST       Hostname, IPv4 or IPv6 address\n"
        "  -P, --port PORT       UDP port (default: 5683)\n"
        "  -D, --debug           Protocol progress on stderr\n"
        "  -J, --json            Compact JSON, one line per status\n"
        "  -I, --int             Encode set values as integers\n"
        "  --timeout SECONDS     Request timeout (default: 10)\n"
        "  --idle-timeout SEC    Observe inactivity timeout (default: 0 = unlimited)\n"
        "  --retries COUNT       Retries after control rejection (default: 5)\n"
        "  --no-resync           Do not synchronize after control rejection\n"
        "  -h, --help            Show this help\n"
        "  --version             Show version\n\n"
        "Values are strings by default; true/false become JSON booleans.\n"
        "With -I, true/false become 1/0, as in the Python original.\n"
        "Stop status-observe with Ctrl+C.\n";
}
long long integer(std::string_view value, const std::string& name) {
    // Python int accepts a leading plus; from_chars does not.
    if (!value.empty() && value.front() == '+') value.remove_prefix(1);
    long long result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw UsageError("Invalid integer for " + name + ": " + std::string(value));
    return result;
}
long long range(std::string_view value, const std::string& name, long long low, long long high) {
    const auto result = integer(value, name);
    if (result < low || result > high) throw UsageError(name + " out of range");
    return result;
}
} // namespace

int main(int argc, char** argv) {
    try {
        std::string host, command;
        aioairctrl::ClientOptions options;
        bool compact = false, as_int = false, resync = true;
        int retries = 5;
        std::vector<std::string> pairs;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto next = [&]() -> std::string {
                if (++i >= argc) throw UsageError("Missing value for " + arg);
                return argv[i];
            };
            if (arg == "-h" || arg == "--help") { help(); return 0; }
            if (arg == "--version") { std::cout << "aioairctrl-cpp 0.1.0\n"; return 0; }
            if (arg == "-H" || arg == "--host") host = next();
            else if (arg == "-P" || arg == "--port") options.port = static_cast<std::uint16_t>(range(next(), arg, 1, 65535));
            else if (arg == "-D" || arg == "--debug")
                options.log = [](const std::string& message) { std::cerr << "[aioairctrl] " << message << '\n'; };
            else if (arg == "-J" || arg == "--json") compact = true;
            else if (arg == "-I" || arg == "--int") as_int = true;
            else if (arg == "--no-resync") resync = false;
            else if (arg == "--retries") retries = static_cast<int>(range(next(), arg, 0, 1000));
            else if (arg == "--timeout") options.timeout = std::chrono::seconds(range(next(), arg, 1, 86400));
            else if (arg == "--idle-timeout") options.observe_idle_timeout = std::chrono::seconds(range(next(), arg, 0, 86400));
            else if (command.empty() && (arg == "status" || arg == "status-observe" || arg == "set")) command = arg;
            else if (command == "set" && arg.find('=') != std::string::npos && arg.front() != '-') pairs.push_back(arg);
            else throw UsageError("Unknown argument: " + arg);
        }
        if (host.empty() || command.empty()) throw UsageError("--host and a command are required; use --help");
        if (as_int && command != "set") throw UsageError("--int is only valid for set");
        if (compact && command == "set") throw UsageError("--json is only valid for status/status-observe");
        aioairctrl::Json data = aioairctrl::Json::object();
        if (command == "set") {
            if (pairs.empty()) throw UsageError("set requires KEY=VALUE");
            for (const auto& pair : pairs) {
                const auto split = pair.find('=');
                const auto key = pair.substr(0, split), value = pair.substr(split + 1);
                if (key.empty()) throw UsageError("Control key must not be empty");
                if (as_int) data[key] = value == "true" ? 1 : value == "false" ? 0 : integer(value, key);
                else if (value == "true" || value == "false") data[key] = value == "true";
                else data[key] = value;
            }
        }
        // Validate all CLI input before contacting the device.
        aioairctrl::Client client(host, options);
        const auto output = [compact](const aioairctrl::Json& status) {
            std::cout << status.dump(compact ? -1 : 2) << std::endl;
            if (!std::cout) throw std::runtime_error("Could not write status output");
            return true;
        };
        if (command == "status") output(client.get_status());
        else if (command == "status-observe") {
            std::signal(SIGINT, stop_handler);
            std::signal(SIGTERM, stop_handler);
            client.observe_status(output, [] { return stopped != 0; });
        } else if (!client.set_control_values(data, retries, resync)) {
            std::cerr << "Device rejected control values after all attempts\n";
            return 1;
        }
        return 0;
    } catch (const UsageError& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 2;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
