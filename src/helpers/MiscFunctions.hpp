#pragma once
#include <string>

std::string execAndGet(const char* cmd);
void addHyprlandNotification(const std::string& icon, float timeMs, const std::string& color, const std::string& message);
bool fileExists(std::string path);