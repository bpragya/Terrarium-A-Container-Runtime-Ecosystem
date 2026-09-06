#pragma once

// Canonical creature status strings.
// MUST match the CHECK constraint in the creatures schema (creatures.cpp).
namespace state {
    inline constexpr const char* ALIVE    = "ALIVE";     // running
    inline constexpr const char* SLEEPING = "SLEEPING";  // stopped / OOM-killed
    inline constexpr const char* RELEASED = "RELEASED";  // exited cleanly, cleaned up
}
