export module engine.gameplay.rts.economy.systems.energy_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.economy.components.energy_source;
export import engine.gameplay.rts.economy.resources.player_energy;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.status.components.disabled;

// Tallies each player's power, in parallel per chunk, before the tick (the
// original's Energy, kept up to date as objects join and leave their player):
// a consumer counts while it exists; a producer while it is not disabled
// (Object::onDisabledEdge takes its production and active bonuses away),
// with its EnergyBonus per active bonus source. A dying object counts until
// it is removed (it leaves its team in ~Object). The chunks' shares are
// summed once they are done.
export import engine.gameplay.rts.construction.components.under_construction;

export namespace engine::gameplay
{
struct EnergySystem
{
	// Player::becomingTeamMember: a structure still under construction neither makes nor uses power.
	using Query = ecs::Query<ecs::Read<EnergySource>, ecs::Read<Owner>, ecs::Optional<Disabled>, ecs::Exclude<UnderConstruction>>;
	using Resources = ecs::Resources<ecs::Write<EnergyShares>, ecs::Write<PlayerEnergy>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<EnergyShares>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		auto &shares = context.Write<EnergyShares>().Slot(context);
		const auto sources = chunk.Get<EnergySource>();
		const auto owners = chunk.Get<Owner>();
		const auto disabled = chunk.Get<Disabled>();
		for (std::size_t row = 0; row < sources.size(); ++row)
		{
			const EnergySource &source = sources[row];
			if (source.amount < 0)
				shares.push_back({owners[row].player, source.amount});
			else if (source.amount > 0 && (disabled.empty() || disabled[row].mask == 0))
				shares.push_back({owners[row].player, source.amount + source.bonus * source.bonusSources});
		}
	}

	void AfterChunks(Query &, ecs::SystemContext &context) const
	{
		PlayerEnergy &energy = context.Write<PlayerEnergy>();
		energy.Clear();
		energy.Refresh(context.Tick());
		context.Write<EnergyShares>().ForEach([&](const EnergyShare &share) { energy.Add(share.player, share.amount); });
	}
};

// Player::onPowerBrownOutChange / doPowerDisable: a POWERED object is DISABLED_UNDERPOWERED while its player's
// production is below its consumption or its power is sabotaged, and no longer once it is not.
struct BrownOutSystem
{
	using Query = ecs::Query<ecs::Read<Powered>, ecs::Read<Owner>, ecs::Optional<Disabled>>;
	using Resources = ecs::Resources<ecs::Read<PlayerEnergy>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const PlayerEnergy &energy = context.Read<PlayerEnergy>();
		const auto owners = chunk.Get<Owner>();
		const auto disabled = chunk.Get<Disabled>();
		const auto entities = chunk.Entities();
		auto &commands = context.Commands();
		for (std::size_t row = 0; row < owners.size(); ++row)
		{
			const bool brownOut = energy.BrownOut(owners[row].player);
			const std::uint32_t mask = disabled.empty() ? 0u : disabled[row].mask;
			const bool underpowered = (mask & disabled_type::Underpowered) != 0;
			if (brownOut == underpowered)
				continue;
			const std::uint32_t changed = brownOut ? mask | disabled_type::Underpowered : mask & ~disabled_type::Underpowered;
			if (disabled.empty())
				commands.Add<Disabled>(entities[row], Disabled{changed});
			else
				commands.Set<Disabled>(entities[row], Disabled{changed});
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::EnergySystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.energy";
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};

template<>
struct SystemTraits<engine::gameplay::BrownOutSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.brown_out";
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::EnergySystem>;
};
}
