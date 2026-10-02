export module engine.gameplay.rts.vision.components.dynamic_clearing;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;
export import engine.gameplay.rts.vision.definitions.dynamic_clearing;

// An object's shroud clearing range as its DynamicClearingDefinition plays out (DynamicShroudClearingRangeUpdate's
// state): the ticks left of it (m_stateCountDown), the tick it ends whatever happens (m_doneForeverFrame), its range
// as made (m_nativeClearingRange) and as it stands now (m_currentClearingRange), the ticks until its range is next
// set (m_changeIntervalCountdown) and its phase. Its grid (the pseudo-wireframe effect over what it clears: the
// original's GRID_FX_DECAL_COUNT decals) is made on its first update (1) and gone once it holds or is done (2); while
// shown, `gridRadius` is the ring its pieces sit on and `gridOpacity` how opaque they are, as its last update set them
// (animateGridDecals). Simulation state: checkpointed.
export namespace engine::gameplay
{
enum class DynamicClearingState : std::uint8_t
{
	NotStarted,
	Growing,
	Sustaining,
	Shrinking,
	DoneForever,
	Sleeping,
};

struct DynamicClearing
{
	std::int64_t countdown{0};
	std::uint64_t doneForeverTick{0};
	Engine::Math::Fixed native;
	Engine::Math::Fixed current;
	std::uint32_t changeCountdown{0};
	DynamicClearingState state{DynamicClearingState::NotStarted};
	std::uint8_t grid{0}; // 0 not made yet, 1 shown, 2 gone
	std::uint8_t reserved[2]{};
	Engine::Math::Fixed gridRadius;
	Engine::Math::Fixed gridOpacity;
};

// The constructor: its whole time (ShrinkDelay + ShrinkTime) from `now`, and its range as made.
constexpr DynamicClearing StartDynamicClearing(const DynamicClearingDefinition &how, Engine::Math::Fixed native, std::uint64_t now) noexcept
{
	DynamicClearing clearing;
	clearing.countdown = static_cast<std::int64_t>(how.shrinkDelay) + how.shrinkTime;
	clearing.doneForeverTick = now + static_cast<std::uint64_t>(clearing.countdown);
	clearing.native = native;
	return clearing;
}
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::DynamicClearing>
{
	static constexpr std::string_view StableName = "engine.gameplay.dynamic_clearing";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
