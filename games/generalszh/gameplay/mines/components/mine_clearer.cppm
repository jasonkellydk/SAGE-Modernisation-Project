export module games.generalszh.gameplay.mines.components.mine_clearer;
import std;

export import engine.ecs.core.component_registry;
import engine.ecs.system.system;

// A dozer or worker (DozerAIUpdate, WorkerAIUpdate): it carries its MINE_CLEARING_DETAIL weapon set whenever it is not
// at work on a structure ("maybe go clear some mines, if I feel like it"); and that weapon set flag's bit.
export namespace generalszh::gameplay
{
struct MineClearer
{
	std::uint8_t reserved{0};
};

struct MineClearingRules
{
	std::uint32_t weaponFlag{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::MineClearer>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.mine_clearer";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ResourceTraits<generalszh::gameplay::MineClearingRules>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.mine_clearing_rules";
};
}
