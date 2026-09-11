#include <aioairctrl/client.hpp>
#include <charconv>
#include <csignal>
#include <cerrno>
#include <deque>
#include <fcntl.h>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <unistd.h>
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
        "       aioairctrl -H HOST [options] session [-J]\n"
        "       aioairctrl -H HOST [options] set [-I] KEY=VALUE [KEY=VALUE ...]\n\n"
        "  -H, --host HOST       Hostname, IPv4 or IPv6 address\n"
        "  -P, --port PORT       UDP port (default: 5683)\n"
        "  -D, --debug           Protocol progress on stderr\n"
        "  -J, --json            Compact JSON, one line per status\n"
        "  -I, --int             Encode set values as integers\n"
        "  --timeout SECONDS     Request timeout (default: 10)\n"
        "  --control-timeout SEC Control timeout in session mode (default: 10)\n"
        "  --idle-timeout SEC    Observe inactivity timeout (default: 0 = unlimited)\n"
        "  --retries COUNT       Retries after control rejection (default: 5)\n"
        "  --no-resync           Do not synchronize after control rejection\n"
        "  -h, --help            Show this help\n"
        "  --version             Show version\n\n"
        "Values are strings by default; true/false become JSON booleans.\n"
        "With -I, true/false become 1/0, as in the Python original.\n"
        "Session reads JSON control commands from stdin and emits JSON envelopes.\n"
        "Stop status-observe/session with Ctrl+C.\n";
}
long long integer(std::string_view value, const std::string& name) {
    // Python int accepts a leading plus; from_chars does not.
    if (!value.empty() && value.front() == '+') value.remove_prefix(1);
    long long result = 0;
    const std::from_chars_result parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
        throw UsageError("Invalid integer for " + name + ": " + std::string(value));
    return result;
}
long long range(std::string_view value, const std::string& name, long long low, long long high) {
    const long long result = integer(value, name);
    if (result < low || result > high) throw UsageError(name + " out of range");
    return result;
}

struct SessionCommand {
    std::uint64_t id = 0;
    aioairctrl::Json values;
};

class SessionInput {
public:
    SessionInput() {
        const int flags = ::fcntl(STDIN_FILENO, F_GETFL, 0);
        if (flags < 0 || ::fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0)
            throw std::runtime_error("Could not configure session input");
    }
    void pump() {
        char chunk[4096];
        for (;;) {
            const ssize_t size = ::read(STDIN_FILENO, chunk, sizeof(chunk));
            if (size > 0) {
                buffer_.append(chunk, static_cast<std::size_t>(size));
                if (buffer_.size() > 1024 * 1024)
                    throw std::runtime_error("Session input exceeds 1 MiB");
                parse_lines();
                continue;
            }
            if (size == 0) { eof_ = true; return; }
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) return;
            throw std::runtime_error("Could not read session input");
        }
    }
    bool eof() const { return eof_; }
    bool empty() const { return commands_.empty(); }
    SessionCommand pop() {
        SessionCommand command = std::move(commands_.front());
        commands_.pop_front();
        return command;
    }
private:
    void parse_lines() {
        for (;;) {
            const std::string::size_type newline = buffer_.find('\n');
            if (newline == std::string::npos) return;
            std::string line = buffer_.substr(0, newline);
            buffer_.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) continue;
            const aioairctrl::Json object = aioairctrl::Json::parse(line);
            if (!object.is_object() || !object.contains("id") || !object["id"].is_number_unsigned() ||
                !object.contains("values") || !object["values"].is_object() || object["values"].empty())
                throw std::runtime_error("Invalid session control command");
            commands_.push_back({object["id"].get<std::uint64_t>(), object["values"]});
        }
    }
    std::string buffer_;
    std::deque<SessionCommand> commands_;
    bool eof_ = false;
};

void write_envelope(const aioairctrl::Json& object) {
    std::cout << object.dump() << std::endl;
    if (!std::cout) throw std::runtime_error("Could not write session output");
}

void run_session(aioairctrl::Client& client) {
    SessionInput input;
    while (!stopped && !input.eof()) {
        client.observe_status(
            [&](const aioairctrl::Json& status) {
                write_envelope({{"_airctrl", "status"}, {"data", status}});
                input.pump();
                return !stopped && !input.eof() && input.empty();
            },
            [&] {
                input.pump();
                return stopped || input.eof() || !input.empty();
            });
        if (stopped || input.eof()) break;
        while (!input.empty() && !stopped) {
            SessionCommand command = input.pop();
            try {
                const bool accepted = client.set_control_values(command.values, 0, false);
                if (accepted) write_envelope({{"_airctrl", "control"}, {"id", command.id}, {"ok", true}});
                else write_envelope({{"_airctrl", "control"}, {"id", command.id}, {"ok", false},
                                     {"error", "Device rejected control values"}});
            } catch (const std::exception& error) {
                // A control failure does not replace the transport. Observation
                // resumes on the same socket; only a later status timeout ends
                // this process and lets the controller create a fresh session.
                write_envelope({{"_airctrl", "control"}, {"id", command.id}, {"ok", false},
                                {"error", error.what()}});
            }
        }
    }
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
            const std::function<std::string()> next = [&]() -> std::string {
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
            else if (arg == "--control-timeout") options.control_timeout = std::chrono::seconds(range(next(), arg, 1, 86400));
            else if (arg == "--idle-timeout") options.observe_idle_timeout = std::chrono::seconds(range(next(), arg, 0, 86400));
            else if (command.empty() && (arg == "status" || arg == "status-observe" || arg == "session" || arg == "set")) command = arg;
            else if (command == "set" && arg.find('=') != std::string::npos && arg.front() != '-') pairs.push_back(arg);
            else throw UsageError("Unknown argument: " + arg);
        }
        if (host.empty() || command.empty()) throw UsageError("--host and a command are required; use --help");
        if (as_int && command != "set") throw UsageError("--int is only valid for set");
        if (compact && command == "set") throw UsageError("--json is only valid for status/status-observe/session");
        if (command == "set") options.control_timeout = options.timeout;
        aioairctrl::Json data = aioairctrl::Json::object();
        if (command == "set") {
            if (pairs.empty()) throw UsageError("set requires KEY=VALUE");
            for (const std::string& pair : pairs) {
                const std::string::size_type split = pair.find('=');
                const std::string key = pair.substr(0, split), value = pair.substr(split + 1);
                if (key.empty()) throw UsageError("Control key must not be empty");
                if (as_int) data[key] = value == "true" ? 1 : value == "false" ? 0 : integer(value, key);
                else if (value == "true" || value == "false") data[key] = value == "true";
                else data[key] = value;
            }
        }
        // Validate all CLI input before contacting the device.
        aioairctrl::Client client(host, options);
        const aioairctrl::Client::StatusCallback output = [compact](const aioairctrl::Json& status) {
            std::cout << status.dump(compact ? -1 : 2) << std::endl;
            if (!std::cout) throw std::runtime_error("Could not write status output");
            return true;
        };
        if (command == "status") output(client.get_status());
        else if (command == "status-observe") {
            std::signal(SIGINT, stop_handler);
            std::signal(SIGTERM, stop_handler);
            client.observe_status(output, [] { return stopped != 0; });
        } else if (command == "session") {
            std::signal(SIGINT, stop_handler);
            std::signal(SIGTERM, stop_handler);
            run_session(client);
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
