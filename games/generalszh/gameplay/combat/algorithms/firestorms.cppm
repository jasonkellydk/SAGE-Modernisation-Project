export module games.generalszh.gameplay.combat.algorithms.firestorms;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.combat.systems.firestorm_system;
export import games.generalszh.gameplay.effects.resources.effect_cues;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import games.generalszh.content.combat.combat_catalog;
import engine.gameplay.common.spatial.resources.spatial_index;
import engine.gameplay.common.identity.components.owner;
import games.generalszh.gameplay.academy.algorithms.academy_records;

// The tick's FirestormEvents, after the step: its FXList on it (doFXObj); its scorch mark; and doDamageScan: everything
// within its bounding circle (FROM_BOUNDINGSPHERE_2D: to their bounding circles), no higher than MaxHeightForDamage over
// it, takes DamageAmount of FLAME, BURNED, from it (attemptDamage: with the next tick's damage).
export namespace generalszh::gameplay
{
inline void ApplyFirestorms(GameWorld &game)
{
	namespace gp = engine::gameplay;
	using Kind = FirestormEvent::Kind;
	auto &world = game.world;
	auto *resource = world.FindResource<FirestormEvents>();
	if (resource == nullptr)
		return;
	std::vector<FirestormEvent> events;
	resource->AppendTo(events);
	resource->Reset(0);
	const auto *spatial = world.FindResource<gp::SpatialIndex>();
	auto *cues = world.FindResource<EffectCues>();
	const std::uint32_t flame = content::DamageTypeIndex("FLAME").value_or(0);
	const std::uint32_t burned = content::DeathTypeIndex("BURNED").value_or(0);
	for (const FirestormEvent &event : events)
	{
		const FirestormConfig *config = game.templates.FirestormOf(event.definition);
		if (config == nullptr)
			continue;
		switch (event.kind)
		{
		case Kind::Effects:
			if (cues != nullptr && !config->fx.empty())
				cues->list.push_back({config->fx, event.at, event.firestorm});
			// Its effects fired: its player's academy records a firestorm made (recordFirestormCreated).
			if (const auto *owner = world.IsAlive(event.firestorm) ? world.Get<gp::Owner>(event.firestorm) : nullptr)
				RecordAcademy(game, owner->player, AcademyCount::FirestormCreated);
			break;
		case Kind::Scorch:
			if (cues != nullptr && event.radius > Engine::Math::Fixed{})
			{
				EffectCue scorch{{}, event.at, {}};
				scorch.scorch = event.radius;
				cues->list.push_back(std::move(scorch));
			}
			break;
		case Kind::Damage:
		{
			if (spatial == nullptr || event.radius <= Engine::Math::Fixed{})
				break;
			std::vector<gp::SpatialEntry> near;
			// The index finds by centre: widened by the largest footprint, then measured exactly.
			spatial->ForEachWithin(event.at.XY(), event.radius + Engine::Math::Fixed::FromInt(250), [&](const gp::SpatialEntry &entry) { near.push_back(entry); });
			for (const gp::SpatialEntry &entry : near)
			{
				if (entry.entity == event.firestorm)
					continue;
				const Engine::Math::Fixed reach = event.radius + entry.radius;
				if (Engine::Math::DistanceSquared(entry.position.XY(), event.at.XY()) > reach * reach)
					continue;
				if (entry.position.z > event.at.z + config->maxHeight)
					continue;
				DamageFrom(game, entry.entity, event.firestorm, config->damage, flame, burned);
			}
			break;
		}
		}
	}
}
}
