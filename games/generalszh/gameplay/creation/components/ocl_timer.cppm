export module games.generalszh.gameplay.creation.components.ocl_timer;
import std;

export import engine.ecs.core.component_registry;

// Something that runs an object creation list every so often (the original's OCLUpdate: the Supply Drop Zone's cargo
// plane, the Reinforcement Pad's vehicle): the tick of its next run (0: its timer not started) and when that timer
// started; a faction-triggered one also the player it last served (its timer restarts with a new one) and whether it
// waits for one (FactionTriggered: no playable owner, no runs). Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct OclTimer
{
	static constexpr std::uint32_t NoPlayer = 0xFFFFFFFFu;
	std::uint64_t nextTick{0};
	std::uint64_t startedTick{0};
	std::uint32_t player{NoPlayer};
	std::uint32_t neutral{1};
};

// Each definition's OCLUpdate (present or not): its list (OCL), or by the side of its owner (FactionOCL), MinDelay and
// MaxDelay (ticks), CreateAtEdge and FactionTriggered.
struct OclTimerConfig
{
	bool present{false};
	bool atEdge{false};
	bool factionTriggered{false};
	std::uint64_t minDelay{0};
	std::uint64_t maxDelay{0};
	std::string list;
	std::vector<std::pair<std::string, std::string>> factionLists; // side, list
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::OclTimer>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.ocl_timer";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
