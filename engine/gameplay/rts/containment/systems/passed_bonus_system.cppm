export module engine.gameplay.rts.containment.systems.passed_bonus_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.rts.containment.components.transport;

// Weapon::computeBonus's container part (OpenContain WeaponBonusPassedToPassengers: the Battle Bus), each tick before
// the weapons fire: whoever rides in a container that passes on its weapon bonus conditions has them added to its own
// (WeaponBonusConditions::passed); anyone else has none passed. (Its weapons are not re-timed by it, as the original's
// passengers' are not when their container's conditions change.) A batch over the conditions: the containers' are read
// first, then the riders' set.
export namespace engine::gameplay
{
struct PassedBonusSystem
{
	using Query = ecs::Query<ecs::Write<WeaponBonusConditions>, ecs::Optional<OffMap>, ecs::Optional<Transport>>;

	void Execute(Query &query, ecs::SystemContext &) const
	{
		std::vector<std::pair<ecs::Entity, std::uint32_t>> passing;
		query.ForEachChunk([&](auto chunk) {
			const auto transports = chunk.template Get<Transport>();
			if (transports.empty())
				return;
			const auto conditions = chunk.template Get<WeaponBonusConditions>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < conditions.size(); ++row)
				if (transports[row].definition.bonusToPassengers)
					passing.emplace_back(entities[row], conditions[row].flags);
		});
		query.ForEachChunk([&](auto chunk) {
			auto conditions = chunk.template Get<WeaponBonusConditions>();
			const auto away = chunk.template Get<OffMap>();
			for (std::size_t row = 0; row < conditions.size(); ++row)
			{
				std::uint32_t passed = 0;
				if (!away.empty())
					for (const auto &[holder, flags] : passing)
						if (holder == away[row].holder)
						{
							passed = flags;
							break;
						}
				conditions[row].passed = passed;
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::PassedBonusSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.passed_bonus";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it before the weapons.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
