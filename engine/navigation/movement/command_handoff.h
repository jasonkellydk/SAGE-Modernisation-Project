#pragma once

namespace navigation {
// A replacement route is allowed to take over only after the search has
// produced it. Until then, the current route remains the locomotor target.
// This is the command-level interpolation contract: no null-goal frame is
// inserted between the old and new routes.
inline bool retainRouteDuringCommandHandoff(bool hasCurrentRoute,
    bool replacementPending) noexcept
{
    return hasCurrentRoute && replacementPending;
}
}
