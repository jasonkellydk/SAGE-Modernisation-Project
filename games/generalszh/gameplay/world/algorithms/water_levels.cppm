export module games.generalszh.gameplay.world.algorithms.water_levels;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.world.resources.water_changes;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import games.generalszh.content.combat.combat_catalog;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.resources.spatial_index;

// Water tables changed over time (TerrainLogic::changeWaterHeightOverTime / update / setWaterHeight on a polygon's
// water), between ticks:
//   ChangeWaterHeightOverTime (WATER_CHANGE_HEIGHT_OVER_TIME): the area's change replaces any earlier one of it; it moves
//     (target - its height now) / (30 x seconds) a frame (none past MAX_DYNAMIC_WATER);
//   UpdateWaterLevels (TerrainLogic::update, newest first): each moves its height by its change; the step that reaches
//     (or passes) its target sets the target and ends it. The area's height is its polygon's points' (whole units). A
//     rise deals its damage (DAMAGE_WATER, DEATH_NORMAL, from no one) to everything in the area's bounding circle standing
//     where the ground is under the water (isUnderwater: its height itself not looked at), on the final step and else
//     only on frames that are whole seconds. True when an area's height changed: the pathfinding map is remade
//     (forceMapRecalculation).
export namespace generalszh::gameplay
{
namespace water_detail
{
namespace gp = engine::gameplay;
using Engine::Math::Fixed;

inline bool SetWaterHeight(GameWorld &game, std::uint32_t area, Fixed height, Fixed damage)
{
	const auto &areas = game.ground.WaterAreas();
	if (area >= areas.size())
		return false;
	const Fixed previous = areas[area].height;
	game.ground.SetWaterHeight(area, height);
	const Fixed now = game.ground.WaterAreas()[area].height;
	if (damage > Fixed{} && now > previous)
	{
		const auto &water = game.ground.WaterAreas()[area];
		const Engine::Math::FixedVector2 centre{(water.low.x + water.high.x) / Fixed::FromInt(2), (water.low.y + water.high.y) / Fixed::FromInt(2)};
		const Fixed reach = Engine::Math::Length(water.high - water.low);
		const std::uint32_t type = content::DamageTypeIndex("WATER").value_or(0);
		const std::uint32_t death = content::DeathTypeIndex("NORMAL").value_or(0);
		std::vector<ecs::Entity> hit;
		game.world.Resource<gp::SpatialIndex>().ForEachWithin(centre, reach, [&](const gp::SpatialEntry &entry) {
			if (Engine::Math::DistanceSquared(entry.position.XY(), centre) <= reach * reach && game.ground.Underwater(entry.position.XY()))
				hit.push_back(entry.entity);
		});
		for (const ecs::Entity entity : hit)
			DamageFrom(game, entity, {}, damage, type, death);
	}
	return now != previous;
}
}

inline void ChangeWaterHeightOverTime(GameWorld &game, std::string_view name, Engine::Math::Fixed height, Engine::Math::Fixed seconds, Engine::Math::Fixed damage)
{
	auto &changes = game.world.Resource<WaterChanges>().list;
	const auto area = game.ground.WaterNamed(name);
	if (!area || changes.size() >= WaterChanges::Max)
		return;
	std::erase_if(changes, [&](const WaterChange &change) { return change.area == *area; });
	WaterChange change;
	change.area = static_cast<std::uint32_t>(*area);
	change.current = game.ground.WaterAreas()[*area].height;
	const Engine::Math::Fixed frames = Engine::Math::Fixed::FromInt(30) * seconds;
	change.changePerFrame = frames > Engine::Math::Fixed{} ? (height - change.current) / frames : height - change.current;
	change.target = height;
	change.damage = damage;
	changes.push_back(change);
}

// doWaterChangeHeight (WATER_CHANGE_HEIGHT): TerrainLogic::setWaterHeight at once with 999999.9 damage (all that the
// rise puts under the water dies) and the pathfinding map remade (forcePathfindUpdate); an unknown area does nothing. A
// change of it under way goes on.
inline void ChangeWaterHeight(GameWorld &game, std::string_view name, Engine::Math::Fixed height)
{
	const auto area = game.ground.WaterNamed(name);
	if (!area)
		return;
	water_detail::SetWaterHeight(game, static_cast<std::uint32_t>(*area), height, Engine::Math::Fixed::FromRatio(9999999, 10));
	game.world.Resource<WaterChanges>().refresh = 1;
}

inline bool UpdateWaterLevels(GameWorld &game)
{
	using Engine::Math::Fixed;
	auto &resource = game.world.Resource<WaterChanges>();
	auto &changes = resource.list;
	const bool forced = resource.refresh != 0;
	resource.refresh = 0;
	if (changes.empty())
		return forced;
	bool changed = forced;
	const bool damageFrame = game.tick % 30 == 0;
	for (std::size_t index = changes.size(); index-- > 0;)
	{
		WaterChange &change = changes[index];
		const bool final = change.changePerFrame > Fixed{} ? change.current + change.changePerFrame >= change.target
														   : change.current + change.changePerFrame <= change.target;
		if (final)
		{
			changed = water_detail::SetWaterHeight(game, change.area, change.target, change.damage) || changed;
			changes[index] = changes.back();
			changes.pop_back();
			continue;
		}
		change.current += change.changePerFrame;
		changed = water_detail::SetWaterHeight(game, change.area, change.current, damageFrame ? change.damage : Fixed{}) || changed;
	}
	return changed;
}
}
