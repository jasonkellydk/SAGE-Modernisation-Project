export module games.generalszh.gameplay.combat.algorithms.weapon_bonus_pulses;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.combat.systems.weapon_bonus_pulse_system;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.resources.spatial_index;
import engine.gameplay.common.weapons.components.temp_weapon_bonus;
import engine.gameplay.rts.containment.resources.cargo_manifest;

// The tick's WeaponBonusPulses (what WeaponBonusUpdate::update reaches): everything within BonusRange of it (centre to
// centre, 2D) on the map, alive and its ally (its own included), of the kinds (every RequiredAffectKindOf, no
// ForbiddenAffectKindOf) is given BonusConditionType for BonusDuration (doTempWeaponBonus); so is everyone of those kinds
// inside anything found there.
export namespace generalszh::gameplay
{
inline void ApplyWeaponBonusPulses(GameWorld &game)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	auto *resource = world.FindResource<WeaponBonusPulses>();
	if (resource == nullptr)
		return;
	std::vector<WeaponBonusPulseEvent> pulses;
	resource->AppendTo(pulses);
	resource->Reset(0);
	const auto *spatial = world.FindResource<gp::SpatialIndex>();
	const auto *relationships = world.FindResource<gp::Relationships>();
	if (pulses.empty() || spatial == nullptr || relationships == nullptr)
		return;
	const auto kindsFit = [&](ecs::Entity entity, const WeaponBonusPulseConfig &config) {
		const auto *ref = world.Get<gp::DefinitionRef>(entity);
		if (ref == nullptr)
			return false;
		const content::KindOfMask &kinds = game.templates.DefinitionAt(ref->index).kinds;
		for (std::size_t word = 0; word < kinds.size(); ++word)
			if ((kinds[word] & config.required[word]) != config.required[word] || (kinds[word] & config.forbidden[word]) != 0)
				return false;
		return true;
	};
	const auto give = [&](ecs::Entity entity, const WeaponBonusPulseConfig &config) {
		if (!world.Has<gp::WeaponBonusConditions>(entity))
			world.Add<gp::WeaponBonusConditions>(entity);
		if (!world.Has<gp::TempWeaponBonus>(entity))
			world.Add<gp::TempWeaponBonus>(entity);
		gp::GiveTempWeaponBonus(*world.Get<gp::TempWeaponBonus>(entity), *world.Get<gp::WeaponBonusConditions>(entity), config.bit, config.durationTicks,
			game.tick);
	};
	for (const WeaponBonusPulseEvent &pulse : pulses)
	{
		const WeaponBonusPulseConfig *config = game.templates.WeaponBonusPulseOf(pulse.definition);
		if (config == nullptr)
			continue;
		const std::uint32_t player = pulse.player;
		const std::uint32_t myTeam = pulse.team;
		const Engine::Math::FixedVector2 centre = pulse.centre;
		const Engine::Math::Fixed range = config->range;
		std::vector<ecs::Entity> found;
		spatial->ForEachWithin(centre, range, [&](const gp::SpatialEntry &entry) {
			if (Engine::Math::DistanceSquared(entry.position.XY(), centre) <= range * range)
				found.push_back(entry.entity);
		});
		for (const ecs::Entity entity : found)
		{
			const auto *theirs = world.IsAlive(entity) ? world.Get<gp::Owner>(entity) : nullptr;
			if (theirs == nullptr || EffectivelyDead(game, entity))
				continue;
			const auto *theirTeam = world.Get<gp::TeamMember>(entity);
			if (!relationships->Allies(myTeam, player, theirTeam != nullptr ? theirTeam->team : gp::Relationships::NoTeam, theirs->player))
				continue;
			if (kindsFit(entity, *config))
				give(entity, *config);
			const auto aboard = game.manifest.Aboard(entity);
			for (const ecs::Entity rider : std::vector<ecs::Entity>(aboard.begin(), aboard.end()))
				if (world.IsAlive(rider) && kindsFit(rider, *config))
					give(rider, *config);
		}
	}
}
}
