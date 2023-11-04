#pragma once
#include <string>
#include <sdbus-c++/Message.h>

std::string execAndGet(const char* cmd);
void addHyprlandNotification(const std::string& icon, float timeMs, const std::string& color, const std::string& message);
bool inShellPath(const std::string& exec);
void sendEmptyDbusMethodReply(sdbus::MethodCall& call, u_int32_t responseCode);