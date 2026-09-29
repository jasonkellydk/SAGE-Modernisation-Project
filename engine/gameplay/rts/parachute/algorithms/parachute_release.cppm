export module engine.gameplay.rts.parachute.algorithms.parachute_release;
import std;
export import engine.gameplay.common.spatial.components.carried;

export import engine.gameplay.rts.parachute.components.parachute;
export import engine.gameplay.rts.parachute.definitions.parachute_definition;
export import engine.gameplay.common.physics.components.physics_body;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.health.components.pending_damage;
export import engine.gameplay.rts.navigation.resources.navigation_grid;
export import engine.gameplay.rts.lifecycle.resources.kill_requests;

// A rider let go by its parachute (ParachuteContain::onRemoving, and onDie before it): no longer held, allowed to fall
// (in free fall too when the chute was lost aloft: onDie's setIsInFreeFall). Where it is let go, it drowns (DAMAGE_WATER,
// DEATH_FLOODED, HUGE_DAMAGE_AMOUNT) at or below the water's surface plus the chute's slop, and is killed off the map or
// over a cliff, water or impassable cell. A rider lost aloft also takes the chute's FreeFallDamagePercent of its
// maximum as falling damage (DAMAGE_FALLING, DEATH_SPLATTED) from the chute's killer, after the rest.
export namespace engine::gameplay
{
struct RiderLoss
{
	ecs::Entity source;
	Engine::Math::Fixed amount;
	std::uint32_t damageType{0};
	std::uint32_t deathType{0};
};

template<class Lookup, class Commands>
void ReleaseRider(ecs::Entity rider, const Engine::Math::FixedVector3 &at, const ParachuteDefinition &definition, const Lookup &lookup,
	Commands &commands, const GroundHeight &ground, const NavigationGrid &grid, KillRequests &kills, bool freeFall,
	std::optional<RiderLoss> loss = std::nullopt)
{
	using Engine::Math::Fixed;
	commands.template Remove<ParachuteRider>(rider);
	commands.template Remove<Carried>(rider);
	// Falling freely it is DISABLED_FREEFALL at once (PhysicsBehavior::update, on its next step while above the ground):
	// its locomotor lets go of it.
	if (const Disabled *disabled = lookup.template Get<Disabled>(rider))
		commands.template Set<Disabled>(rider, Disabled{(disabled->mask & ~disabled_type::Held) | (freeFall ? disabled_type::Freefall : 0u)});
	if (const PhysicsBody *body = lookup.template Get<PhysicsBody>(rider))
	{
		PhysicsBody falling = *body;
		falling.Set(physics_flag::AllowToFall, true);
		if (freeFall)
			falling.Set(physics_flag::InFreeFall, true);
		commands.template Set<PhysicsBody>(rider, falling);
	}
	Fixed water;
	if (ground.Water(at.XY(), water) && at.z <= water + definition.waterSlop)
	{
		commands.template Add<PendingDamage>(rider, PendingDamage{{}, HugeDamage(), definition.drownDamageType, definition.drownDeathType});
		return;
	}
	const auto cell = [](Fixed value) { return static_cast<std::int32_t>((value / Fixed::FromInt(PathfindCellSize)).Floor()); };
	const std::int32_t x = cell(at.x), y = cell(at.y);
	bool unusable = !grid.Contains(x, y);
	if (!unusable)
	{
		const PathfindCellType type = grid.Type(x, y);
		unusable = type == PathfindCellType::Cliff || type == PathfindCellType::Water || type == PathfindCellType::Impassable;
	}
	if (unusable)
	{
		kills.entities.push_back(rider);
		return;
	}
	if (loss && loss->amount > Fixed{})
		commands.template Add<PendingDamage>(rider, PendingDamage{loss->source, loss->amount, loss->damageType, loss->deathType});
}
}
