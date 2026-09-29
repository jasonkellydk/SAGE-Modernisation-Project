export module engine.gameplay.rts.radar.systems.radar_coverage_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.radar.components.radar_provider;
export import engine.gameplay.rts.radar.resources.player_radar;
export import engine.gameplay.rts.economy.systems.energy_system;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.status.components.disabled;

// Player::hasRadar, as the original's counts leave it: an object gives its player radar while it is not disabled
// (Object::setDisabled removes it for any disabling), until it is removed (RadarUpgrade::onDelete); a player short of
// power (onPowerBrownOutChange: disableRadar) keeps radar only through a DisableProof one.
export namespace engine::gameplay
{
struct RadarCoverageSystem
{
	using Query = ecs::Query<ecs::Read<RadarProvider>, ecs::Read<Owner>, ecs::Optional<Disabled>>;
	using Resources = ecs::Resources<ecs::Read<PlayerEnergy>, ecs::Write<PlayerRadar>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const PlayerEnergy &energy = context.Read<PlayerEnergy>();
		PlayerRadar &radar = context.Write<PlayerRadar>();
		std::vector<std::uint8_t> count, proof;
		query.ForEachChunk([&](auto chunk) {
			const auto providers = chunk.template Get<RadarProvider>();
			const auto owners = chunk.template Get<Owner>();
			const auto disabled = chunk.template Get<Disabled>();
			for (std::size_t row = 0; row < providers.size(); ++row)
			{
				if (!disabled.empty() && disabled[row].mask != 0)
					continue;
				const std::uint32_t player = owners[row].player;
				if (player >= count.size())
				{
					count.resize(player + 1, 0);
					proof.resize(player + 1, 0);
				}
				count[player] = 1;
				if (providers[row].disableProof != 0)
					proof[player] = 1;
			}
		});
		radar.has.assign(count.size(), 0);
		for (std::uint32_t player = 0; player < count.size(); ++player)
			radar.has[player] = count[player] != 0 && (energy.Sufficient(player) || proof[player] != 0) ? 1 : 0;
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::RadarCoverageSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.radar_coverage";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::EnergySystem>;
};
}
