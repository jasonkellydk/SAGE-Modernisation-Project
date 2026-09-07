#pragma once

#if defined(RTS_PROFILE_TRACY)
#include <tracy/Tracy.hpp>
#define GRAPHICS_PROFILE_SCOPE(name) ZoneScopedN(name)
#else
#define GRAPHICS_PROFILE_SCOPE(name) ((void)0)
#endif
