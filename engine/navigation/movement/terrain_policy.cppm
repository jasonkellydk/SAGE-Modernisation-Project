module;
#include <cstdint>

export module engine.navigation.movement.terrain_policy;

export namespace navigation {
enum class TerrainKind : unsigned char {
    ground, water, cliff, rubble, obstacle, blockedBridge, impassable
};
enum SurfaceMask : std::uint32_t {
    groundSurface=1, waterSurface=2, cliffSurface=4, airSurface=8, rubbleSurface=16
};
struct TerrainQuery {
    std::uint32_t surfaces=0, ignoredObstacle=0;
    bool crusher=false;
};
struct TerrainCell {
    TerrainKind kind=TerrainKind::ground;
    std::uint32_t obstacle=0;
    bool valid=false, fence=false;
};
constexpr std::uint32_t terrainSurfaces(TerrainKind kind) {
    switch (kind) {
    case TerrainKind::ground: return groundSurface|airSurface;
    case TerrainKind::water: return waterSurface|airSurface;
    case TerrainKind::cliff: return cliffSurface|airSurface;
    case TerrainKind::rubble: return rubbleSurface|airSurface;
    case TerrainKind::obstacle:
    case TerrainKind::blockedBridge:
    case TerrainKind::impassable: return airSurface;
    default: return 0;
    }
}
// Terrain permission only; footprint, occupancy and destination rules are separate.
constexpr bool permitsTerrain(TerrainQuery query,TerrainCell cell) {
    if (!cell.valid) return false;
    if (query.ignoredObstacle && cell.kind==TerrainKind::obstacle &&
        cell.obstacle==query.ignoredObstacle) return true;
    if (query.crusher && cell.fence) return true;
    return (terrainSurfaces(cell.kind)&query.surfaces)!=0;
}
}
