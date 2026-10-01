export module games.generalszh.gameplay.mines.algorithms.mine_clearing;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.effects.resources.effect_cues;
export import engine.gameplay.rts.combat.resources.shots;
export import engine.gameplay.rts.combat.systems.targeting_system;
export import engine.gameplay.rts.mines.components.minefield;
export import engine.gameplay.common.lifetime.components.lifetime;
export import engine.gameplay.common.identity.components.team_member;

// Mine clearing: the tick's disarms (Weapon::privateFireWeapon's DAMAGE_DISARM) carried out, and the search a weapon
// with a ContinueAttackRange makes for what to clear next (AIAttackState's continue range, privateAttackPosition).
export namespace generalszh::gameplay
{
namespace mine_clearing_detail
{
namespace gp = engine::gameplay;

// PartitionManager::getClosestObject(at, range, FROM_CENTER_2D) with PartitionFilterPossibleToAttack(ATTACK_NEW_TARGET,
// `source`, `from`) (and PartitionFilterSamePlayer `player` when given): the closest (centre to centre, within `range`,
// the first of equals) that is not unattackable, an enemy's, not stealthed from it unless it sees through stealth, not
// NO_ATTACK_FROM_AI to its own AI's command, of a kind its weapons may target and reckoned hurt by one of them
// (getAbleToAttackSpecificObject).
inline ecs::Entity Closest(GameWorld &game, ecs::Entity source, Engine::Math::FixedVector2 at, Engine::Math::Fixed range, gp::CommandSource from,
	bool ignoringStealth, std::optional<std::uint32_t> player, ecs::Entity skip)
{
	auto &world = game.world;
	const auto *spatial = world.FindResource<gp::SpatialIndex>();
	const auto *relationships = world.FindResource<gp::Relationships>();
	const auto *armament = world.Get<gp::Armament>(source);
	const auto *owner = world.Get<gp::Owner>(source);
	const auto *transform = world.Get<gp::Transform>(source);
	if (spatial == nullptr || relationships == nullptr || armament == nullptr || owner == nullptr || transform == nullptr ||
		armament->weapon == gp::WeaponCatalog::None)
		return {};
	const auto *member = world.Get<gp::TeamMember>(source);
	const auto *body = world.Get<gp::Targetable>(source);
	const auto *set = world.Get<gp::WeaponSlots>(source);
	const auto *conditions = world.Get<gp::WeaponBonusConditions>(source);
	const std::uint32_t bonus = conditions != nullptr ? conditions->Effective() : 0u;
	const gp::WeaponDefinition reach = gp::TargetingSystem::Reach(game.templates.weapons, *armament, set, bonus);
	const ecs::EntityLookup<gp::TargetingSystem::Lookup> lookup(world);
	const auto gate = gp::TargetingSystem::AttackGate::For(game.templates.weapons, game.templates.armors, *armament, set,
		gp::PitchBodyOf(transform->position, world.Get<gp::BodyExtent>(source)), body != nullptr ? body->classes : 0u, bonus, lookup);
	const std::uint32_t team = member != nullptr ? member->team : gp::Relationships::NoTeam;
	std::uint32_t refused = gp::target_class::Unattackable | gp::target_class::Undetected;
	if (!ignoringStealth)
		refused |= gp::target_class::Hidden;
	if (from == gp::CommandSource::Ai)
		refused |= gp::target_class::NoAttackFromAi;
	const gp::SpatialEntry *best = nullptr;
	Engine::Math::Fixed bestDistance;
	spatial->ForEachWithin(at, range, [&](const gp::SpatialEntry &entry) {
		if (entry.entity == source || entry.entity == skip || (entry.classes & refused) != 0 || (player && entry.player != *player))
			return;
		const Engine::Math::Fixed distance = Engine::Math::DistanceSquared(entry.position.XY(), at);
		if (distance > range * range || (best != nullptr && distance >= bestDistance))
			return;
		if (!relationships->Enemies(team, owner->player, entry.team, entry.player) || !gp::CanTarget(reach, entry.classes) || !gate.Allows(entry))
			return;
		best = &entry;
		bestDistance = distance;
	});
	return best != nullptr ? best->entity : ecs::Entity{};
}

// MinefieldBehavior::disarm: one that does not regenerate is removed; one that does is brought down to MIN_HEALTH 0.1
// by unresistable damage of its own that sets nothing off (its auto-heal then counts from it), its mines all spent,
// rubble and masked.
inline void DisarmMinefield(GameWorld &game, ecs::Entity victim, gp::Minefield &mine)
{
	auto &world = game.world;
	if (mine.regenerates == 0)
	{
		if (!world.Has<gp::Lifetime>(victim))
			world.Add<gp::Lifetime>(victim);
		*world.Get<gp::Lifetime>(victim) = gp::Lifetime{game.tick, 1, 0};
		mine.remaining = 0;
		return;
	}
	if (auto *health = world.Get<gp::Health>(victim))
	{
		const Engine::Math::Fixed least = Engine::Math::Fixed::FromRatio(1, 10);
		if (health->current > least)
		{
			health->current = least;
			health->lastDamageTick = game.tick;
			health->lastAttacker = victim;
			health->lastDamageType = game.templates.weapons.unresistable;
		}
		mine.lastHealth = health->current;
	}
	mine.remaining = 0;
}
}

// Weapon::privateFireWeapon's DAMAGE_DISARM for the tick's disarms: at a minefield, its fire FX plays there and it is
// disarmed; else a mine, booby trap or demo trap is removed (with the FX). (The academy's cleared-mine count: not yet.)
// Then AIAttackState's ContinueAttackRange: the victim gone or spent, an attack still on it goes on to the closest of the
// victim's player's it may attack within that range of where it stood (no longer seeing through stealth), if any.
inline void ApplyDisarms(GameWorld &game)
{
	namespace gp = engine::gameplay;
	using namespace mine_clearing_detail;
	auto &world = game.world;
	auto *resource = world.FindResource<gp::Disarms>();
	if (resource == nullptr)
		return;
	std::vector<gp::Disarm> events;
	resource->AppendTo(events);
	resource->Reset(0);
	auto *cues = world.FindResource<EffectCues>();
	for (const gp::Disarm &event : events)
	{
		if (!world.IsAlive(event.victim))
			continue;
		const auto *ref = world.Get<gp::DefinitionRef>(event.victim);
		const auto *victimOwner = world.Get<gp::Owner>(event.victim);
		const content::WeaponContent *content = game.templates.WeaponContentAt(event.weapon);
		const auto fx = [&] {
			if (cues != nullptr && content != nullptr && !content->FireFX(event.veterancy).empty())
				cues->list.push_back({content->FireFX(event.veterancy), world.Get<gp::Transform>(event.victim)->position, ecs::Entity{}});
		};
		bool gone = false;
		if (auto *mine = world.Get<gp::Minefield>(event.victim))
		{
			fx();
			DisarmMinefield(game, event.victim, *mine);
			gone = true;
		}
		else if (ref != nullptr)
		{
			const content::ObjectDefinition &kind = game.templates.DefinitionAt(ref->index);
			if (kind.Is("MINE") || kind.Is("BOOBY_TRAP") || kind.Is("DEMOTRAP"))
			{
				fx();
				if (!world.Has<gp::Lifetime>(event.victim))
					world.Add<gp::Lifetime>(event.victim);
				*world.Get<gp::Lifetime>(event.victim) = gp::Lifetime{game.tick, 1, 0};
				gone = true;
			}
		}
		const gp::WeaponDefinition &weapon = game.templates.weapons.At(event.weapon);
		auto *attack = world.IsAlive(event.source) ? world.Get<gp::AttackTarget>(event.source) : nullptr;
		if (!gone || weapon.continueAttackRange <= Engine::Math::Fixed{} || attack == nullptr || attack->target != event.victim || victimOwner == nullptr)
			continue;
		const ecs::Entity next = Closest(game, event.source, event.at.XY(), weapon.continueAttackRange, attack->source, false, victimOwner->player, event.victim);
		if (next != ecs::Entity{})
			attack->target = next;
	}
}
}
