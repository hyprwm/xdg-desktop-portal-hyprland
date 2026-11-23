#pragma once

#include <hyprutils/memory/Atomic.hpp>
#include <hyprutils/memory/SharedPtr.hpp>
#include <hyprutils/memory/UniquePtr.hpp>
#include <hyprutils/memory/WeakPtr.hpp>

template <typename T>
using SP = Hyprutils::Memory::CSharedPointer<T>;
template <typename T>
using WP = Hyprutils::Memory::CWeakPointer<T>;
template <typename T>
using UP = Hyprutils::Memory::CUniquePointer<T>;
template <typename T>
using ASP = Hyprutils::Memory::CAtomicSharedPointer<T>;
