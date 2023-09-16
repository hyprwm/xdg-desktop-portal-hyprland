#include "Timer.hpp"

CTimer::CTimer(float ms, std::function<void()> callback) {
    m_fDuration  = ms;
    m_tStart     = std::chrono::high_resolution_clock::now();
    m_fnCallback = callback;
}

bool CTimer::passed() const {
    return std::chrono::high_resolution_clock::now() > (m_tStart + std::chrono::milliseconds((uint64_t)m_fDuration));
}

float CTimer::passedMs() const {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::high_resolution_clock::now() - m_tStart).count();
}

float CTimer::duration() const {
    return m_fDuration;
}