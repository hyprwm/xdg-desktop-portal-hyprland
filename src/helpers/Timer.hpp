#pragma once

#include <functional>
#include <chrono>

class CTimer {
  public:
    CTimer(float ms, std::function<void()> callback);

    bool                  passed() const;
    float                 passedMs() const;
    float                 duration() const;

    std::function<void()> m_fnCallback;

  private:
    std::chrono::high_resolution_clock::time_point m_tStart;
    float                                          m_fDuration;
};