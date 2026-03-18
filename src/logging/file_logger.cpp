#include "amf/logging/file_logger.hpp"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace amf {

FileLogger::FileLogger(std::string log_path)
    : log_path_(std::move(log_path)) {}

const std::string& FileLogger::path() const {
    return log_path_;
}

bool FileLogger::log(LogLevel level, const std::string& message) {
    std::lock_guard<std::mutex> lock(mutex_);

    std::ofstream stream(log_path_, std::ios::app);
    if (!stream.is_open()) {
        return false;
    }

    stream << now_utc() << " [" << to_string(level) << "] " << message << "\n";
    return true;
}

const char* FileLogger::to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Info:
            return "INFO";
        case LogLevel::Warning:
            return "WARN";
        case LogLevel::Error:
            return "ERROR";
    }

    return "UNKNOWN";
}

std::string FileLogger::now_utc() {
    const auto now = std::chrono::system_clock::now();
    const auto now_time = std::chrono::system_clock::to_time_t(now);

    std::tm tm_utc {};
#if defined(_WIN32)
    gmtime_s(&tm_utc, &now_time);
#else
    gmtime_r(&now_time, &tm_utc);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm_utc, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

}  // namespace amf
