#include "MiscFunctions.hpp"
#include "../helpers/Log.hpp"

#include <unistd.h>
#include <filesystem>
#include <cstdlib>
#include <vector>
#include <string>
#include <algorithm>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <uuid/uuid.h>

#include <hyprutils/os/Process.hpp>
using namespace Hyprutils::OS;

std::string execAndGet(const char* cmd) {
    std::string command = cmd + std::string{" 2>&1"};
    CProcess    proc("/bin/sh", {"-c", cmd});

    if (!proc.runSync())
        return "error";

    return proc.stdOut();
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

std::string getRandomUUID() {
    std::string uuid;
    uuid_t      uuid_;
    uuid_generate_random(uuid_);
    return std::format("{:02x}{:02x}{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}-{:02x}{:02x}{:02x}{:02x}{:02x}{:02x}", (uint16_t)uuid_[0], (uint16_t)uuid_[1],
                       (uint16_t)uuid_[2], (uint16_t)uuid_[3], (uint16_t)uuid_[4], (uint16_t)uuid_[5], (uint16_t)uuid_[6], (uint16_t)uuid_[7], (uint16_t)uuid_[8],
                       (uint16_t)uuid_[9], (uint16_t)uuid_[10], (uint16_t)uuid_[11], (uint16_t)uuid_[12], (uint16_t)uuid_[13], (uint16_t)uuid_[14], (uint16_t)uuid_[15]);
}

std::pair<int, std::string> openExclusiveShm() {
    // Only absolute paths can be shared across different shm_open() calls
    std::string name = "/" + getRandomUUID();

    for (size_t i = 0; i < 69; ++i) {
        int fd = shm_open(name.c_str(), O_RDWR | O_CREAT | O_EXCL, 0600);
        if (fd >= 0)
            return {fd, name};
    }

    return {-1, ""};
}

int allocateSHMFile(size_t len) {
    auto [fd, name] = openExclusiveShm();
    if (fd < 0)
        return -1;

    shm_unlink(name.c_str());

    int ret;
    do {
        ret = ftruncate(fd, len);
    } while (ret < 0 && errno == EINTR);

    if (ret < 0) {
        close(fd);
        return -1;
    }

    return fd;
}

bool allocateSHMFilePair(size_t size, int* rw_fd_ptr, int* ro_fd_ptr) {
    auto [fd, name] = openExclusiveShm();
    if (fd < 0) {
        return false;
    }

    // CLOEXEC is guaranteed to be set by shm_open
    int ro_fd = shm_open(name.c_str(), O_RDONLY, 0);
    if (ro_fd < 0) {
        shm_unlink(name.c_str());
        close(fd);
        return false;
    }

    shm_unlink(name.c_str());

    // Make sure the file cannot be re-opened in read-write mode (e.g. via
    // "/proc/self/fd/" on Linux)
    if (fchmod(fd, 0) != 0) {
        close(fd);
        close(ro_fd);
        return false;
    }

    int ret;
    do {
        ret = ftruncate(fd, size);
    } while (ret < 0 && errno == EINTR);
    if (ret < 0) {
        close(fd);
        close(ro_fd);
        return false;
    }

    *rw_fd_ptr = fd;
    *ro_fd_ptr = ro_fd;
    return true;
}
