export module games.generalszh.gameplay.combat_drop.algorithms.rappel_landings;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.combat_drop.components.combat_drop;
export import games.generalszh.gameplay.effects.resources.effect_cues;
import games.generalszh.gameplay.combat_drop.algorithms.combat_drop_orders;
import games.generalszh.gameplay.containment.algorithms.garrisons;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import games.generalszh.gameplay.powers.algorithms.special_power_state;
import games.generalszh.content.combat.combat_catalog;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.algorithms.find_position;
import engine.gameplay.common.spatial.resources.spatial_index;
import engine.gameplay.common.health.components.health;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.containment.components.garrison;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.lifecycle.resources.kill_requests;
import Engine.Core.Math.FixedRandom;

// AIRappelState::update as a rappeller lands (after the step), onto a building it dropped into (on the ground it just
// stands: its AI idles):
//   killEnemiesInContainer: the first enemy inside is taken out and killed (DAMAGE_UNRESISTABLE, DEATH_NORMAL) as the
//     rappeller's kill (scoreTheKill), then the next, at most two; any killed, the rappeller's CombatDropKillFX
//     (UnitSpecificFX) plays on the building; two killed, the rappeller dies too (it trades itself for them);
//   else it goes inside if the building takes it (GarrisonContain::isValidContainerFor with room: who may go in, a
//     building alive and not really damaged unless GARRISONABLE_UNTIL_DESTROYED, not a NO_GARRISON rappeller, a free
//     place; no care for whose it is) and stands at the building (onContaining);
//   else it is put on the ground beside it: out from its centre by the smaller of their bounding radii at a random
//     angle between half and all the way round (one draw), facing as the building does, and walks to the nearest legal
//     place around there (findPositionAround from three quarters round out to 200) if there is one.
// The kills die with the next tick's casualties.
export namespace generalszh::gameplay
{
inline void ApplyRappelLandings(GameWorld &game, const RappelLandings &landings)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	auto &world = game.world;
	const std::uint32_t unresistable = content::DamageTypeIndex("UNRESISTABLE").value_or(0);
	const std::uint32_t normal = content::DeathTypeIndex("NORMAL").value_or(0);
	landings.ForEach([&](const RappelLanding &landing) {
		const ecs::Entity rappeller = landing.rappeller;
		const ecs::Entity building = landing.building;
		if (!world.IsAlive(rappeller) || building == ecs::Entity{} || !world.IsAlive(building) || !world.Has<gp::Transport>(building))
			return;
		const gp::Transform where = *world.Get<gp::Transform>(building);
		int killed = 0;
		while (killed < 2)
		{
			ecs::Entity enemy;
			for (const ecs::Entity occupant : game.manifest.Aboard(building))
				if (world.IsAlive(occupant) && RelationOf(game, rappeller, occupant) == gp::Relationship::Enemies)
				{
					enemy = occupant;
					break;
				}
			if (enemy == ecs::Entity{})
				break;
			TakeOutNow(game, building, enemy);
			game.kills.typed.push_back({enemy, unresistable, normal, rappeller});
			++killed;
		}
		const auto *ref = world.Get<gp::DefinitionRef>(rappeller);
		if (killed > 0 && ref != nullptr)
			if (const std::string_view fx = game.templates.DefinitionAt(ref->index).UnitFx("CombatDropKillFX"); !fx.empty())
				if (auto *cues = world.FindResource<EffectCues>())
					cues->list.push_back({std::string(fx), where.position, building});
		if (killed == 2)
		{
			game.kills.typed.push_back({rappeller, unresistable, normal, {}});
			return;
		}
		// isValidContainerFor(obj, TRUE).
		const gp::Transport &room = *world.Get<gp::Transport>(building);
		bool valid = !room.closed && room.occupied < room.definition.slots && MayContain(game, building, rappeller);
		if (valid)
			if (const gp::Garrison *garrison = world.Get<gp::Garrison>(building))
			{
				const gp::Health *health = world.Get<gp::Health>(building);
				const Fixed reallyDamaged = game.templates.Content().gameData.unitReallyDamaged;
				valid = health != nullptr && health->current > Fixed{} && (garrison->untilDestroyed || health->current > health->maximum * reallyDamaged) &&
					(ref == nullptr || !game.templates.DefinitionAt(ref->index).Is("NO_GARRISON"));
			}
		if (valid)
		{
			PutInside(game, building, rappeller);
			world.Get<gp::Transform>(rappeller)->position = where.position;
			return;
		}
		const auto &spatial = world.Resource<gp::SpatialIndex>();
		const gp::SpatialEntry *riderEntry = spatial.Find(rappeller);
		const gp::SpatialEntry *buildingEntry = spatial.Find(building);
		const Fixed offset = std::min(riderEntry != nullptr ? riderEntry->radius : Fixed{}, buildingEntry != nullptr ? buildingEntry->radius : Fixed{});
		// GameLogicRandomValueReal(PI, 2PI): half to all the way round.
		const Engine::Math::TurnAngle angle{static_cast<std::uint32_t>(Engine::Math::UniformInt(game.random, 0x80000000ll, 0xFFFFFFFFll))};
		const auto start = where.position.XY() + Engine::Math::Direction(angle) * offset;
		gp::Transform &placed = *world.Get<gp::Transform>(rappeller);
		placed.position = {start.x, start.y, game.ground.At(start)};
		placed.facing = where.facing;
		if (const auto spot = gp::FindPositionAround(start, Fixed{}, Fixed::FromInt(200), Engine::Math::TurnAngle{0xC0000000u}, SpotLegal(game)))
			OrderMove(game, rappeller, *spot, false, false);
	});
}
}
