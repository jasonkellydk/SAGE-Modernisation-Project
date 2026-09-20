#pragma once

#include <cstdint>
#include <limits>

namespace navigation {
// The locomotor reports whether it is turning; collision speed limiting is
// a separate reason that a unit can remain blocked on its current route.
inline int retainBlockedFrames(int frames, bool locomotorBlocked, bool collisionLimited) noexcept
{
    return !locomotorBlocked && !collisionLimited && frames > 1 ? 1 : frames;
}

// AI-generated move-away orders are recovery actions, not steering updates.
// Hysteresis prevents two touching units from replacing each other's command
// every collision callback and visibly twitching in place.
inline bool allowBlockedRecovery(std::uint32_t frame, std::uint32_t lastFrame,
    std::uint32_t cooldown=8) noexcept
{
    return lastFrame == std::numeric_limits<std::uint32_t>::max() ||
        (frame >= lastFrame && frame-lastFrame >= cooldown);
}
}
