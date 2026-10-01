export module games.generalszh.gameplay.mines.systems.mine_clearing_detail_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.mines.components.mine_clearer;
export import engine.gameplay.rts.loadout.components.loadout;
export import engine.gameplay.rts.loadout.systems.loadout_system;
export import engine.gameplay.rts.construction.components.builder;

// DozerAIUpdate / WorkerAIUpdate and their states, chunk-parallel: a dozer or worker at work on a structure
// (DozerActionDoActionState: "no mine clearing fun while I'm on the job") drops its MINE_CLEARING_DETAIL weapon set
// flag; one with no task, done with it, idle or harvesting takes it up. On its way to a task it keeps what it had.
export namespace generalszh::gameplay
{
struct MineClearingDetailSystem
{
	using Query = ecs::Query<ecs::Read<MineClearer>, ecs::Write<engine::gameplay::Loadout>, ecs::Optional<engine::gameplay::Builder>>;
	using Resources = ecs::Resources<ecs::Read<MineClearingRules>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const std::uint32_t flag = context.Read<MineClearingRules>().weaponFlag;
		auto loadouts = chunk.Get<engine::gameplay::Loadout>();
		const auto builders = chunk.Get<engine::gameplay::Builder>();
		for (std::size_t row = 0; row < loadouts.size(); ++row)
		{
			if (builders.empty())
				loadouts[row].weaponFlags |= flag;
			else if (builders[row].atWork != 0)
				loadouts[row].weaponFlags &= ~flag;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::MineClearingDetailSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.mine_clearing_detail";
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<engine::gameplay::LoadoutSystem>;
	using After = SystemTypeList<>;
};
}
