export module games.generalszh.gameplay.ai.algorithms.retaliation;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.ai.resources.retaliation_modes;
import games.generalszh.gameplay.ai.algorithms.guards;
import games.generalszh.gameplay.teams.algorithms.team_states;
import engine.gameplay.common.health.systems.health_system;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.targetable;
import engine.gameplay.common.spatial.resources.spatial_index;
import engine.gameplay.common.status.components.ai_activity;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.common.weapons.components.weapon_slots;
import engine.gameplay.common.weapons.resources.weapon_catalog;
import engine.gameplay.rts.combat.components.aggression;
import engine.gameplay.rts.combat.systems.targeting_system;
import engine.gameplay.rts.stealth.components.stealth;

// ActiveBody::attemptDamage's call for help, once the tick has run, for each of its hits in order (the victim alive or
// dead): a victim whose player is human with retaliation mode on (Player::isLogicalRetaliationModeEnabled) and struck
// by an aggressor it should retaliate against (shouldRetaliateAgainstAggressor: there, not airborne, an enemy of the
// victim's, within MaxRetaliationDistance of it between their bounding circles, the victim no DRONE) calls on its
// player's and its allies' things on the map within RetaliationFriendsRadius plus its bounding circle of its centre
// (itself too): each that should retaliate (shouldRetaliate: not CANNOT_RETALIATE, IMMOBILE or a DRONE, with an AI,
// idle, not stealthed unless detected, not using an ability) and may attack the aggressor (an enemy it has a weapon
// for) guards where it stands against it (aiGuardRetaliate, CMD_FROM_AI). Once one retaliates it is no longer idle, so a
// later hit of the tick passes it by.
export namespace generalszh::gameplay
{
namespace retaliation_detail
{
namespace gp = engine::gameplay;

inline bool ShouldRetaliate(GameWorld &game, ecs::Entity them)
{
	auto &world = game.world;
	const auto *ref = world.Get<gp::DefinitionRef>(them);
	if (ref == nullptr)
		return false;
	const content::ObjectDefinition &kind = game.templates.DefinitionAt(ref->index);
	if (kind.Is("CANNOT_RETALIATE") || kind.Is("IMMOBILE") || kind.Is("DRONE"))
		return false;
	if (!HasAi(game, them) || !IsIdle(game, them))
		return false;
	if (const auto *stealth = world.Get<gp::Stealth>(them);
		stealth != nullptr && stealth->Has(gp::stealth_flag::Stealthed) && !stealth->Has(gp::stealth_flag::Detected))
		return false;
	const auto *activity = world.Get<gp::AiActivity>(them);
	return activity == nullptr || activity->usingAbility == 0;
}

// getAbleToAttackSpecificObject(ATTACK_NEW_TARGET, CMD_FROM_AI): possible, or possible after moving.
inline bool MayAttack(GameWorld &game, ecs::Entity them, const gp::SpatialEntry &aggressor)
{
	auto &world = game.world;
	const auto *armament = world.Get<gp::Armament>(them);
	const auto *aggression = world.Get<gp::Aggression>(them);
	const auto *member = world.Get<gp::TeamMember>(them);
	const auto *owner = world.Get<gp::Owner>(them);
	const auto *relationships = world.FindResource<gp::Relationships>();
	if (armament == nullptr || aggression == nullptr || member == nullptr || owner == nullptr || relationships == nullptr)
		return false;
	const gp::WeaponDefinition weapon = gp::TargetingSystem::Reach(game.templates.weapons, *armament, world.Get<gp::WeaponSlots>(them), 0);
	return gp::TargetingSystem::Acceptable(*relationships, aggressor, them, member->team, owner->player, weapon, *aggression);
}
}

inline void ApplyRetaliation(GameWorld &game)
{
	using namespace retaliation_detail;
	using Engine::Math::Fixed;
	auto &world = game.world;
	const auto *hits = world.FindResource<gp::Hits>();
	const auto *modes = world.FindResource<RetaliationModes>();
	const auto *spatial = world.FindResource<gp::SpatialIndex>();
	const auto *relationships = world.FindResource<gp::Relationships>();
	if (hits == nullptr || modes == nullptr || modes->enabled == 0 || spatial == nullptr || relationships == nullptr)
		return;
	std::vector<gp::Hit> list;
	hits->ForEach([&](const gp::Hit &hit) {
		if (hit.source != ecs::Entity{})
			list.push_back(hit);
	});
	const content::AiData &data = game.templates.Content().aiData;
	for (const gp::Hit &hit : list)
	{
		const ecs::Entity victim = hit.target, aggressor = hit.source;
		const auto *owner = world.IsAlive(victim) ? world.Get<gp::Owner>(victim) : nullptr;
		if (owner == nullptr || !modes->On(owner->player) || owner->player >= game.roster.PlayerCount() || !game.roster.PlayerAt(owner->player).human)
			continue;
		// shouldRetaliateAgainstAggressor.
		const gp::SpatialEntry *theirs = world.IsAlive(aggressor) ? spatial->Find(aggressor) : nullptr;
		const gp::SpatialEntry *mine = spatial->Find(victim);
		const auto *victimMember = world.Get<gp::TeamMember>(victim);
		const auto *victimRef = world.Get<gp::DefinitionRef>(victim);
		const auto *victimAt = world.Get<gp::Transform>(victim);
		if (theirs == nullptr || victimRef == nullptr || victimAt == nullptr || victimMember == nullptr ||
			(theirs->classes & (gp::target_class::AirborneVehicle | gp::target_class::AirborneInfantry)) != 0)
			continue;
		if (!relationships->Enemies(theirs->team, theirs->player, victimMember->team, owner->player))
			continue;
		const content::ObjectDefinition &victimKind = game.templates.DefinitionAt(victimRef->index);
		const Fixed victimRadius = content::BoundingCircleRadius(victimKind.geometry);
		const Fixed apart = std::max(Fixed{}, Engine::Math::Distance(theirs->position.XY(), victimAt->position.XY()) - theirs->radius -
			(mine != nullptr ? mine->radius : victimRadius));
		if (apart > data.maxRetaliateDistance || victimKind.Is("DRONE"))
			continue;
		// Its player's and its allies' things within reach, on the map.
		const Fixed reach = data.retaliateFriendsRadius + victimRadius;
		std::vector<ecs::Entity> friends;
		spatial->ForEachWithin(victimAt->position.XY(), reach, [&](const gp::SpatialEntry &entry) {
			if (Engine::Math::DistanceSquared(entry.position.XY(), victimAt->position.XY()) > reach * reach)
				return;
			if (entry.player != owner->player && !relationships->Allies(owner->player, entry.player))
				return;
			friends.push_back(entry.entity);
		});
		std::ranges::sort(friends, [](ecs::Entity a, ecs::Entity b) { return a.index < b.index; });
		for (const ecs::Entity them : friends)
		{
			if (!world.IsAlive(them) || !ShouldRetaliate(game, them) || !MayAttack(game, them, *theirs))
				continue;
			GuardRetaliate(game, them, aggressor, world.Get<gp::Transform>(them)->position.XY());
		}
	}
}
}
