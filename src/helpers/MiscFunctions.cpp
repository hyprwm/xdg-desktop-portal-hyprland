#include "MiscFunctions.hpp"
#include "../helpers/Log.hpp"

#include <unistd.h>
#include <filesystem>
#include <cstdlib>
#include <vector>
#include <string>
#include <algorithm>

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

bool inShellPath(const std::string& exec) {

    if (exec.starts_with("/") || exec.starts_with("./") || exec.starts_with("../"))
        return std::filesystem::exists(exec);

    // we are relative to our PATH
    const char* path = std::getenv("PATH");

    if (!path)
        return false;

    // collect paths
    std::string              pathString = path;
    std::vector<std::string> paths;
    uint32_t                 nextBegin = 0;
    for (uint32_t i = 0; i < pathString.size(); i++) {
        if (path[i] == ':') {
            paths.push_back(pathString.substr(nextBegin, i - nextBegin));
            nextBegin = i + 1;
        }
    }

    if (nextBegin < pathString.size())
        paths.push_back(pathString.substr(nextBegin, pathString.size() - nextBegin));

    return std::ranges::any_of(paths, [&exec](std::string& path) { return access((path + "/" + exec).c_str(), X_OK) == 0; });
}

void sendEmptyDbusMethodReply(sdbus::MethodCall& call, u_int32_t responseCode) {
    auto reply = call.createReply();
    reply << (uint32_t)responseCode;
    reply.send();
}
