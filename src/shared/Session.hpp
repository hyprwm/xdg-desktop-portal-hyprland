#pragma once

#include <sdbus-c++/sdbus-c++.h>

struct SDBusSession {
    std::unique_ptr<sdbus::IObject> object;
    sdbus::ObjectPath               handle;
    std::function<void()>           onDestroy;
};

struct SDBusRequest {
    std::unique_ptr<sdbus::IObject> object;
    sdbus::ObjectPath               handle;
    std::function<void()>           onDestroy;
};

std::unique_ptr<SDBusSession> createDBusSession(sdbus::ObjectPath handle);
std::unique_ptr<SDBusRequest> createDBusRequest(sdbus::ObjectPath handle);