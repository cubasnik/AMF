#pragma once

#include <mutex>
#include <string>

namespace amf {

enum class LogLevel {
    Info,
    Warning,
    Error,
};

class FileLogger {
public:
    explicit FileLogger(std::string log_path);

    const std::string& path() const;
    bool log(LogLevel level, const std::string& message);

private:
    static const char* to_string(LogLevel level);
    static std::string now_utc();

    std::string log_path_;
    std::mutex mutex_;
};

}  // namespace amf
