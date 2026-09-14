/**
 * @file statuslog.cpp
 * @brief Durable, line-oriented CSV status logging.
 */
#include "statuslog.h"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <unordered_set>
#include <utility>

namespace airctrl {
namespace {
constexpr const char* timestampColumn = "timestamp";
constexpr const char* extraColumn = "_extra_json";

std::string csvField(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
    std::string escaped;
    escaped.reserve(value.size() + 2U);
    escaped.push_back('"');
    for (const char character : value) {
        if (character == '"') escaped.push_back('"');
        escaped.push_back(character);
    }
    escaped.push_back('"');
    return escaped;
}

std::string valueField(const nlohmann::json& value) {
    if (value.is_null()) return {};
    if (value.is_boolean()) return value.get<bool>() ? "1" : "0";
    if (value.is_number()) return value.dump();
    if (value.is_string()) {
        const std::string text = value.get<std::string>();
        // Keep every record on one physical line so the plotter can stream it.
        return text.find_first_of("\r\n") == std::string::npos ? text : value.dump();
    }
    return value.dump();
}

std::string utcTimestamp() {
    const std::chrono::system_clock::time_point now = std::chrono::system_clock::now();
    const std::time_t seconds = std::chrono::system_clock::to_time_t(now);
    const long milliseconds = static_cast<long>(
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000);
    std::tm utc{};
    gmtime_r(&seconds, &utc);
    char date[32]{};
    std::strftime(date, sizeof(date), "%Y-%m-%dT%H:%M:%S", &utc);
    char timestamp[40]{};
    std::snprintf(timestamp, sizeof(timestamp), "%s.%03ldZ", date, milliseconds);
    return timestamp;
}

bool parseCsvLine(const std::string& line, std::vector<std::string>* fields) {
    fields->clear();
    std::string field;
    bool quoted = false;
    for (std::size_t index = 0; index < line.size(); ++index) {
        const char character = line[index];
        if (quoted) {
            if (character == '"') {
                if (index + 1U < line.size() && line[index + 1U] == '"') {
                    field.push_back('"');
                    ++index;
                } else quoted = false;
            } else field.push_back(character);
        } else if (character == ',') {
            fields->push_back(std::move(field));
            field.clear();
        } else if (character == '"' && field.empty()) quoted = true;
        else field.push_back(character);
    }
    if (quoted) return false;
    fields->push_back(std::move(field));
    return true;
}

bool writeAll(int descriptor, const std::string& data, std::string* error) {
    std::size_t offset = 0;
    while (offset < data.size()) {
        const ssize_t written = write(descriptor, data.data() + offset, data.size() - offset);
        if (written > 0) {
            offset += static_cast<std::size_t>(written);
            continue;
        }
        if (written < 0 && errno == EINTR) continue;
        if (error) *error = std::string("Schreiben fehlgeschlagen: ") + std::strerror(errno);
        return false;
    }
    return true;
}
} // namespace

StatusCsvLog::StatusCsvLog(std::string path) : path_(std::move(path)) {}

bool StatusCsvLog::initialize(std::string* error) {
    const int descriptor = open(path_.c_str(), O_WRONLY | O_APPEND | O_CREAT | O_CLOEXEC | O_NOFOLLOW, 0640);
    if (descriptor < 0) {
        if (error) *error = "Statusprotokoll kann nicht geöffnet werden: " + path_ + ": " + std::strerror(errno);
        return false;
    }
    struct stat information{};
    const bool regular = fstat(descriptor, &information) == 0 && S_ISREG(information.st_mode);
    const int closeResult = close(descriptor);
    if (!regular) {
        if (error) *error = "Statusprotokoll ist keine reguläre Datei: " + path_;
        return false;
    }
    if (closeResult != 0) {
        if (error) *error = "Statusprotokoll kann nicht geschlossen werden: " + path_;
        return false;
    }
    return loadHeader(error);
}

bool StatusCsvLog::loadHeader(std::string* error) {
    columns_.clear();
    std::ifstream stream(path_, std::ios::binary);
    if (!stream) {
        if (error) *error = "Statusprotokoll kann nicht gelesen werden: " + path_;
        return false;
    }
    stream.seekg(0, std::ios::end);
    const std::streamoff size = stream.tellg();
    if (size == 0) return true;
    if (size < 0) {
        if (error) *error = "Größe des Statusprotokolls kann nicht gelesen werden: " + path_;
        return false;
    }
    stream.seekg(-1, std::ios::end);
    char finalCharacter = 0;
    stream.get(finalCharacter);
    if (finalCharacter != '\n') {
        if (error) *error = "Statusprotokoll endet nicht mit einem Zeilenumbruch: " + path_;
        return false;
    }
    stream.clear();
    stream.seekg(0, std::ios::beg);
    std::string header;
    if (!std::getline(stream, header)) {
        if (error) *error = "CSV-Kopfzeile kann nicht gelesen werden: " + path_;
        return false;
    }
    if (!header.empty() && header.back() == '\r') header.pop_back();
    std::vector<std::string> fields;
    if (!parseCsvLine(header, &fields) || fields.size() < 2U || fields.front() != timestampColumn ||
        fields.back() != extraColumn) {
        if (error) *error = "Ungültige CSV-Kopfzeile im Statusprotokoll: " + path_;
        return false;
    }
    std::unordered_set<std::string> seen;
    for (std::size_t index = 1U; index + 1U < fields.size(); ++index) {
        if (fields[index].empty() || fields[index] == timestampColumn || fields[index] == extraColumn ||
            !seen.insert(fields[index]).second) {
            if (error) *error = "Doppelte oder ungültige CSV-Spalte im Statusprotokoll: " + path_;
            return false;
        }
        columns_.push_back(std::move(fields[index]));
    }
    return true;
}

bool StatusCsvLog::writeRecord(const std::string& record, std::string* error) const {
    const int descriptor = open(path_.c_str(), O_WRONLY | O_APPEND | O_CLOEXEC | O_NOFOLLOW);
    if (descriptor < 0) {
        if (error) *error = "Statusprotokoll kann nicht geöffnet werden: " + path_ + ": " + std::strerror(errno);
        return false;
    }
    bool ok = true;
    if (flock(descriptor, LOCK_EX) != 0) {
        if (error) *error = "Statusprotokoll kann nicht gesperrt werden: " + path_ + ": " + std::strerror(errno);
        ok = false;
    } else {
        struct stat information{};
        if (fstat(descriptor, &information) != 0) {
            if (error) *error = "Größe des Statusprotokolls kann nicht gelesen werden: " + path_;
            ok = false;
        }
        std::string output;
        if (ok && information.st_size == 0) {
            output = timestampColumn;
            for (const std::string& column : columns_) output += ',' + csvField(column);
            output += ',';
            output += extraColumn;
            output.push_back('\n');
        }
        output += record;
        if (ok) ok = writeAll(descriptor, output, error);
        if (flock(descriptor, LOCK_UN) != 0 && ok) {
            if (error) *error = "Sperre des Statusprotokolls kann nicht gelöst werden: " + path_;
            ok = false;
        }
    }
    if (close(descriptor) != 0 && ok) {
        if (error) *error = "Statusprotokoll kann nicht geschlossen werden: " + path_;
        ok = false;
    }
    return ok;
}

bool StatusCsvLog::append(const nlohmann::json& status, std::string* error) {
    if (!status.is_object() || status.empty()) {
        if (error) *error = "Nur ein nichtleerer Objektstatus darf protokolliert werden.";
        return false;
    }
    std::string record;
    if (columns_.empty()) {
        for (nlohmann::json::const_iterator entry = status.begin(); entry != status.end(); ++entry) {
            if (entry.key() != timestampColumn && entry.key() != extraColumn)
                columns_.push_back(entry.key());
        }
        std::sort(columns_.begin(), columns_.end());
    }

    record = csvField(utcTimestamp());
    nlohmann::json extra = nlohmann::json::object();
    std::unordered_set<std::string> regular(columns_.begin(), columns_.end());
    for (const std::string& column : columns_) {
        record.push_back(',');
        const nlohmann::json::const_iterator found = status.find(column);
        if (found != status.end()) record += csvField(valueField(*found));
    }
    for (nlohmann::json::const_iterator entry = status.begin(); entry != status.end(); ++entry) {
        if (regular.find(entry.key()) == regular.end()) extra[entry.key()] = entry.value();
    }
    record.push_back(',');
    if (!extra.empty()) record += csvField(extra.dump());
    record.push_back('\n');
    if (writeRecord(record, error)) return true;
    return false;
}

} // namespace airctrl
