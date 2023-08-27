#pragma once
#include <format>
#include <iostream>
#include <string>

enum eLogLevel
{
    TRACE = 0,
    INFO,
    LOG,
    WARN,
    ERR,
    CRIT
};

namespace Debug {
    template <typename... Args>
    void log(eLogLevel level, const std::string& fmt, Args&&... args) {
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