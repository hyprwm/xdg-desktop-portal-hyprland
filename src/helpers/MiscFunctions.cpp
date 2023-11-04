#include "MiscFunctions.hpp"
#include <memory>
#include <unistd.h>
#include "../helpers/Log.hpp"

std::string execAndGet(const char* cmd) {
    Debug::log(LOG, "execAndGet: {}", cmd);

    std::array<char, 128>                          buffer;
    std::string                                    result;
    const std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(cmd, "r"), pclose);
    if (!pipe) {
        Debug::log(ERR, "execAndGet: failed in pipe");
        return "";
    }
    while (fgets(buffer.data(), buffer.size(), pipe.get()) != nullptr) {
        result += buffer.data();
    }
    return result;
}

void addHyprlandNotification(const std::string& icon, float timeMs, const std::string& color, const std::string& message) {
    const std::string CMD = std::format("hyprctl notify {} {} {} \"{}\"", icon, timeMs, color, message);
    Debug::log(LOG, "addHyprlandNotification: {}", CMD);
    if (fork() == 0)
        execl("/bin/sh", "/bin/sh", "-c", CMD.c_str(), nullptr);
}

bool fileExists(std::string path) {
    return access(path.c_str(), F_OK) == 0;
}