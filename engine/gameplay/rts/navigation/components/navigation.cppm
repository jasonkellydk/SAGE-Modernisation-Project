export module engine.gameplay.rts.navigation.components.navigation;
import std;

export import engine.ecs.core.component_registry;
export import engine.gameplay.rts.navigation.resources.navigation_grid;
export import engine.gameplay.common.spatial.components.surface_layer;

// What navigation knows of an entity:
//   NavigationAgent: a ground mover routed over the grid (the locomotor
//   surfaces it may use and how many cells its footprint reaches);
//   NavigationObstacle: something the grid routes around (its footprint, and
//   whether it is stamped in yet);
//   Route: the smoothed route for its current destination, walked point by
//   point (a long route keeps its first points; reaching the last of them
//   plans the rest).
export namespace engine::gameplay
{
struct NavigationAgent
{
	std::uint8_t surfaces{locomotor_surface::Ground};
	std::uint8_t radius{0};
};

struct NavigationObstacle
{
	ObstacleFootprint footprint;
	bool stamped{false};
	std::uint8_t reserved[7]{}; // no padding: checkpoints hold its bytes
};

inline constexpr std::size_t RoutePoints = 16;

struct Route
{
	Engine::Math::FixedVector2 destination; // the destination it was planned for
	std::array<Engine::Math::FixedVector2, RoutePoints> points{};
	std::array<std::uint8_t, RoutePoints> layers{}; // each point's layer (a deck's, or the ground's)
	std::uint8_t count{0};
	std::uint8_t next{0};
	bool complete{false}; // its last point is the destination (or as near as it could get)
	bool planned{false};
	std::uint8_t reserved[4]{}; // no padding: checkpoints hold its bytes
	std::uint64_t plannedTick{0};
};

// A destination that moved (a chased target) is planned for again at most this often; in between the route's end
// follows it.
inline constexpr std::uint64_t RouteReplanTicks = 10;
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::NavigationAgent>
{
	static constexpr std::string_view StableName = "engine.gameplay.navigation_agent";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::NavigationAgent &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64((std::uint64_t{value.surfaces} << 8) | value.radius);
	}
};

template<>
struct ComponentTraits<engine::gameplay::NavigationObstacle>
{
	static constexpr std::string_view StableName = "engine.gameplay.navigation_obstacle";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::NavigationObstacle &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.footprint.majorRadius.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.footprint.minorRadius.Raw()));
		// Whether it is stamped is the grid's (derived) state, rebuilt on restore: not hashed.
		hasher.AppendU64(static_cast<std::uint64_t>(value.footprint.shape));
	}
};

template<>
struct ComponentTraits<engine::gameplay::Route>
{
	static constexpr std::string_view StableName = "engine.gameplay.route";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Route &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.destination.x.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.destination.y.Raw()));
		for (std::size_t index = 0; index < value.count; ++index)
		{
			hasher.AppendU64(static_cast<std::uint64_t>(value.points[index].x.Raw()));
			hasher.AppendU64(static_cast<std::uint64_t>(value.points[index].y.Raw()));
			if (value.layers[index] != engine::gameplay::GroundLayer)
				hasher.AppendU64(value.layers[index]);
		}
		hasher.AppendU64((std::uint64_t{value.count} << 24) | (std::uint64_t{value.next} << 16) | (value.complete ? 2u : 0u) | (value.planned ? 1u : 0u));
		hasher.AppendU64(value.plannedTick);
	}
};
}
