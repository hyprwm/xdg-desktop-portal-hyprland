#pragma once
#include <format>
#include <iostream>
#include <string>

enum eLogLevel {
    TRACE = 0,
    INFO,
    LOG,
    WARN,
    ERR,
    CRIT
};

#define RASSERT(expr, reason, ...)                                                                                                                                                 \
    if (!(expr)) {                                                                                                                                                                 \
        Debug::log(CRIT, "\n==========================================================================================\nASSERTION FAILED! \n\n{}\n\nat: line {} in {}",            \
                   std::format(reason, ##__VA_ARGS__), __LINE__,                                                                                                                   \
                   ([]() constexpr -> std::string { return std::string(__FILE__).substr(std::string(__FILE__).find_last_of('/') + 1); })().c_str());                               \
        printf("Assertion failed! See the log in /tmp/hypr/hyprland.log for more info.");                                                                                          \
        abort(); /* so that we crash and get a coredump */                                                                                                                         \
    }

#define ASSERT(expr) RASSERT(expr, "?")

namespace Debug {
    inline bool quiet   = false;
    inline bool verbose = false;

    template <typename... Args>
    void log(eLogLevel level, const std::string& fmt, Args&&... args) {

        if (!verbose && level == TRACE)
            return;

        if (quiet)
            return;

        std::cout << '[';

        switch (level) {
            case TRACE: std::cout << "TRACE"; break;
            case INFO: std::cout << "INFO"; break;
            case LOG: std::cout << "LOG"; break;
            case WARN: std::cout << "WARN"; break;
            case ERR: std::cout << "ERR"; break;
            case CRIT: std::cout << "CRITICAL"; break;
        }

        std::cout << "] ";

        std::cout << std::vformat(fmt, std::make_format_args(args...)) << "\n";
    }
};