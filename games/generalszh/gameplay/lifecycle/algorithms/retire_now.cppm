export module games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.rts.lifecycle.algorithms.retirement;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.health.components.pending_damage;
import engine.gameplay.common.health.components.health;
import engine.gameplay.rts.death.components.dying;
import games.generalszh.content.combat.combat_catalog;

// Removes entities between ticks (scripted deletes), through the same
// retirement routine the removal system uses during the step.
export namespace generalszh::gameplay
{
void RetireNow(GameWorld &game, std::vector<ecs::Entity> entities, engine::gameplay::Departure departure = engine::gameplay::Departure::Removed)
{
	std::vector<engine::gameplay::Retiree> retirees;
	for (const ecs::Entity entity : entities)
		retirees.push_back({entity, {}, departure});
	auto &world = game.world;
	engine::gameplay::Retire(std::move(retirees), {game.roster, game.names, game.manifest},
		[&](ecs::Entity entity) {
			return engine::gameplay::RetireeState{world.IsAlive(entity), world.IsAlive(entity) ? world.Get<engine::gameplay::TeamMember>(entity) : nullptr,
				world.IsAlive(entity) ? world.Get<engine::gameplay::DefinitionRef>(entity) : nullptr,
				world.IsAlive(entity) ? world.Get<engine::gameplay::Transform>(entity) : nullptr};
		},
		[&](const engine::gameplay::Retiree &retiree) { world.Destroy(retiree.entity); }, game.casualties);
}

// Object::isEffectivelyDead: dying, or no health left.
bool EffectivelyDead(const GameWorld &game, ecs::Entity entity)
{
	if (!game.world.IsAlive(entity) || game.world.Get<engine::gameplay::Dying>(entity) != nullptr)
		return true;
	const auto *health = game.world.Get<engine::gameplay::Health>(entity);
	return health != nullptr && health->current <= Engine::Math::Fixed{};
}

// Script damage (doNamedDamage, Team::damageTeamMembers): `amount` of DAMAGE_UNRESISTABLE, DEATH_NORMAL, from no one,
// dealt in the next tick's damage (added to any already waiting of the same kind).
void DamageNow(GameWorld &game, ecs::Entity entity, Engine::Math::Fixed amount)
{
	namespace gp = engine::gameplay;
	if (!game.world.IsAlive(entity) || game.world.Get<gp::Health>(entity) == nullptr)
		return;
	const std::uint32_t type = content::DamageTypeIndex("UNRESISTABLE").value_or(0);
	const std::uint32_t death = content::DeathTypeIndex("NORMAL").value_or(0);
	if (auto *waiting = game.world.Get<gp::PendingDamage>(entity))
	{
		if (waiting->damageType == type && waiting->deathType == death && !waiting->source.IsValid())
			waiting->amount += amount;
		else
			*waiting = gp::PendingDamage{{}, amount, type, death};
		return;
	}
	game.world.Add<gp::PendingDamage>(entity);
	*game.world.Get<gp::PendingDamage>(entity) = gp::PendingDamage{{}, amount, type, death};
}

// Killed outright: it dies with this tick's casualties.
void KillNow(GameWorld &game, ecs::Entity entity)
{
	if (game.world.IsAlive(entity))
		game.kills.entities.push_back(entity);
}
}
