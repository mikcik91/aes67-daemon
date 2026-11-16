#pragma once

#include <iostream>
#include <mutex>
#include <string_view>

namespace sap {

// Thread-safe logger. All output goes to stderr with a level prefix.
// A single mutex serializes writes so lines from different threads
// never interleave.
class Logger {
  public:
    enum class Level { Info, Warn, Error };

    static void info(std::string_view msg) {
        log(Level::Info, msg);
    }
    static void warn(std::string_view msg) {
        log(Level::Warn, msg);
    }
    static void error(std::string_view msg) {
        log(Level::Error, msg);
    }

  private:
    static void log(Level level, std::string_view msg) {
        static std::mutex mutex;
        std::lock_guard   lock{mutex};
        const char*       prefix = level == Level::Info   ? "[INFO]  "
                                   : level == Level::Warn ? "[WARN]  "
                                                          : "[ERROR] ";
        std::cerr << prefix << msg << '\n';
    }
};

} // namespace sap
