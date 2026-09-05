module;
#include <array>
#include <span>

export module Graphics.Scene.Water.View;

namespace Graphics
{
// Camera transforms use row-major storage and column vectors, as do the
// water shaders. Reflection changes handedness and retains the projection.
export struct WaterView final
{
    std::array<float,4> camera_position{};
    std::array<float,16> reflected_camera{};
    std::array<float,4> reflection_clip_plane{};
    bool underwater = false;

    WaterView(std::span<const float,16> camera_world, float water_height) noexcept
    {
        camera_position = {camera_world[3],camera_world[7],camera_world[11],1};
        underwater = camera_position[2] < water_height;
        const float side = underwater ? -1.0f : 1.0f;
        reflection_clip_plane = {0,0,side,-side*water_height};
        for (unsigned index=0;index<16;++index)
            reflected_camera[index] = camera_world[index];
        for (unsigned column=0;column<4;++column)
            reflected_camera[8+column] = -camera_world[8+column];
        reflected_camera[11] += 2*water_height;
    }
};
}
