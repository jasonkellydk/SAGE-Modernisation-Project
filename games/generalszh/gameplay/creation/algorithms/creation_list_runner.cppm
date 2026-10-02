export module games.generalszh.gameplay.creation.algorithms.creation_list_runner;
import games.generalszh.gameplay.effects.algorithms.radius_decals;
import std;
import engine.gameplay.rts.collision.components.body_collision;
import games.generalszh.gameplay.containment.algorithms.parachuting;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.rts.slaves.algorithms.enslave;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.random.resources.random_seed;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.world.algorithms.level_setup;
import games.generalszh.gameplay.world.algorithms.position_search;
import games.generalszh.gameplay.effects.resources.effect_cues;
import engine.gameplay.common.spatial.algorithms.find_position;
import engine.gameplay.rts.navigation.resources.navigation_grid;
import engine.gameplay.common.spatial.resources.deck_surfaces;
import engine.gameplay.common.spatial.components.surface_layer;
import engine.gameplay.rts.match.resources.match_outcome;
import games.generalszh.content.combat.combat_catalog;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.attitude;
import engine.gameplay.common.physics.algorithms.forces;
import engine.gameplay.common.physics.resources.physics_settings;
import engine.gameplay.common.lifetime.components.lifetime;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.appearance.components.model_override;
import engine.gameplay.common.appearance.components.debris_look;
import engine.gameplay.common.physics.components.bounce_sound;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.rts.movement.components.locomotion;
import engine.gameplay.rts.stealth.components.undetected_defector;
import engine.gameplay.common.spatial.components.targetable;
import games.generalszh.gameplay.containment.algorithms.garrisons;
import engine.gameplay.rts.emp.components.emp_pulse;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import engine.gameplay.rts.combat.resources.shots;
import engine.gameplay.common.weapons.components.weapon_slots;
import engine.gameplay.common.weapons.resources.weapon_catalog;
import engine.gameplay.common.identity.components.owner;
import Engine.Core.Math.FixedAngle;

// Carries out an object creation list between ticks (as the original's
// ObjectCreationList::create for a dying or firing object): every nugget
// makes its pieces on the source's player's default team, placed and thrown
// by its disposition (on the ground, like the source, sent flying, up, out,
// or by a random force), spinning, with its lifetime and health.
// Deterministic: all choices come from the session's stream.
export namespace generalszh::gameplay
{
struct CreationSource
{
	Engine::Math::FixedVector3 position;
	Engine::Math::TurnAngle facing;
	std::uint32_t team{0xFFFFFFFFu}; // the source's team (none: neutral)
	ecs::Entity entity{};             // the source object, when there is one (what it creates serves it: SlavedUpdate)
	std::uint32_t veterancy{0};       // the source's veterancy level (what inherits it takes it)
	std::uint64_t lifetimeTicks{0};   // ObjectCreationList::create's lifetimeFrames: every piece's exact lifetime (0: its own)
	// The secondary point (ObjectCreationList::create's `secondary`: a power's target): FireWeapon and Attack need it.
	std::optional<Engine::Math::FixedVector3> secondary;
};

namespace creation_detail
{
namespace gameplay = engine::gameplay;
using Engine::Math::Fixed;
using Engine::Math::FixedVector3;
using Engine::Math::TurnAngle;
namespace disposition = content::disposition;

// Debris and created objects belong to the source player's default team.
std::uint32_t OwnerTeam(const GameWorld &game, std::uint32_t sourceTeam)
{
	const auto &roster = game.roster;
	if (sourceTeam < roster.TeamCount())
	{
		const std::uint32_t player = roster.TeamAt(sourceTeam).owner;
		if (player < roster.PlayerCount())
			if (const auto team = roster.DefaultTeam(player))
				return *team;
		return sourceTeam;
	}
	if (const auto neutral = roster.FindTeam("team"))
		return *neutral;
	return 0;
}

TurnAngle AnyAngle(Engine::Math::RandomStream &random)
{
	return TurnAngle{static_cast<std::uint32_t>(Engine::Math::UniformInt(random, 0, 0xFFFFFFFFll))};
}

TurnAngle Between(Engine::Math::RandomStream &random, TurnAngle low, TurnAngle high)
{
	const std::int64_t span = static_cast<std::int32_t>((high - low).units);
	return low + TurnAngle{static_cast<std::uint32_t>(Engine::Math::UniformInt(random, std::min<std::int64_t>(0, span), std::max<std::int64_t>(0, span)))};
}

std::int32_t Around(Engine::Math::RandomStream &random, std::int32_t rate)
{
	const std::int64_t magnitude = rate < 0 ? -static_cast<std::int64_t>(rate) : rate;
	return static_cast<std::int32_t>(Engine::Math::UniformInt(random, -magnitude, magnitude));
}

// calcRandomForce: any heading, tilted up by a pitch between the two, a magnitude between the two (drawn in that order).
FixedVector3 RandomForce(Engine::Math::RandomStream &random, Fixed minForce, Fixed maxForce, TurnAngle minPitch, TurnAngle maxPitch)
{
	const TurnAngle heading = AnyAngle(random);
	const TurnAngle tilt = Between(random, minPitch, maxPitch);
	const Fixed magnitude = Engine::Math::UniformFixed(random, minForce, std::max(minForce, maxForce));
	const Fixed flat = magnitude * Engine::Math::Cos(tilt);
	return {flat * Engine::Math::Cos(heading), flat * Engine::Math::Sin(heading), magnitude * Engine::Math::Sin(tilt)};
}

// What a source is moving at (PhysicsBehavior::getVelocity): its body's velocity, or while its locomotor carries it,
// its speed along its facing.
FixedVector3 SourceVelocity(const ecs::World &world, ecs::Entity source)
{
	const gameplay::PhysicsBody *body = world.IsAlive(source) ? world.Get<gameplay::PhysicsBody>(source) : nullptr;
	if (body == nullptr)
		return {};
	if (!body->Has(gameplay::physics_flag::Locomotive))
		return body->velocity;
	const gameplay::Locomotion *motion = world.Get<gameplay::Locomotion>(source);
	const gameplay::Transform *transform = world.Get<gameplay::Transform>(source);
	if (motion == nullptr || transform == nullptr)
		return {};
	return {Engine::Math::Cos(transform->facing) * motion->speed, Engine::Math::Sin(transform->facing) * motion->speed, Fixed{}};
}

// ApplyRandomForceNugget::create: its source (with a body) thrown by a random force, spinning each way up to its
// spin rate. A dead source's locomotor no longer holds it: its body flies on its own.
void ThrowSource(GameWorld &game, const content::CreationNugget &nugget, const CreationSource &source)
{
	auto &world = game.world;
	gameplay::PhysicsBody *body = world.IsAlive(source.entity) ? world.Get<gameplay::PhysicsBody>(source.entity) : nullptr;
	if (body == nullptr)
		return;
	gameplay::ApplyForce(*body, RandomForce(game.random, nugget.minForce, nugget.maxForce, nugget.minPitch, nugget.maxPitch));
	const std::int32_t spin = nugget.spinRate.value_or(0);
	body->yawRate = Around(game.random, spin);
	body->rollRate = Around(game.random, spin);
	body->pitchRate = Around(game.random, spin);
	if (world.Get<gameplay::Dying>(source.entity) != nullptr)
		body->Set(gameplay::physics_flag::Locomotive, false);
}

// Rotates an offset in the source's frame into the world.
FixedVector3 Rotate(const FixedVector3 &offset, TurnAngle facing)
{
	const Fixed c = Engine::Math::Cos(facing), s = Engine::Math::Sin(facing);
	return {offset.x * c - offset.y * s, offset.x * s + offset.y * c, offset.z};
}

// Object::setLayer: the deck it stands on (SurfaceLayer), or the ground.
void SetLayer(GameWorld &game, ecs::Entity entity, std::uint8_t layer)
{
	auto &world = game.world;
	if (layer == gameplay::GroundLayer)
	{
		if (world.Has<gameplay::SurfaceLayer>(entity))
			world.Remove<gameplay::SurfaceLayer>(entity);
		return;
	}
	if (!world.Has<gameplay::SurfaceLayer>(entity))
		world.Add<gameplay::SurfaceLayer>(entity);
	world.Get<gameplay::SurfaceLayer>(entity)->layer = layer;
}

// The original's doStuffToObj: lifetime, health, placement and throw.
void Place(GameWorld &game, ecs::Entity entity, const content::CreationNugget &nugget, const CreationSource &source, bool debris)
{
	auto &world = game.world;
	auto &random = game.random;
	if (source.lifetimeTicks > 0)
	{
		// doStuffToObj: a passed-in lifetime overrides (setLifetimeRange(frames, frames)).
		if (gameplay::Lifetime *life = world.Get<gameplay::Lifetime>(entity))
		{
			(void)Engine::Math::UniformInt(random, static_cast<std::int64_t>(source.lifetimeTicks), static_cast<std::int64_t>(source.lifetimeTicks));
			life->expiresTick = game.tick + source.lifetimeTicks;
		}
	}
	else if (nugget.maxLifetime > 0)
		if (gameplay::Lifetime *life = world.Get<gameplay::Lifetime>(entity))
		{
			const auto delay = Engine::Math::UniformInt(random, static_cast<std::int64_t>(nugget.minLifetime), static_cast<std::int64_t>(nugget.maxLifetime));
			life->expiresTick = game.tick + static_cast<std::uint64_t>(std::max<std::int64_t>(delay, 1));
		}
	// A debris piece's look (DebrisDrawInterface::setModelName / setAnimNames): its player's colour when it may take it,
	// one of its animation sets ("STOP" for the last: hold the flying animation's first frame) and the effect it plays
	// as it lands.
	if (debris)
	{
		gameplay::DebrisLook look;
		look.playerColor = nugget.okToChangeModelColor ? 1u : 0u;
		if (!nugget.animationSets.empty())
		{
			const auto &set = nugget.animationSets[static_cast<std::size_t>(
				Engine::Math::UniformInt(random, 0, static_cast<std::int64_t>(nugget.animationSets.size()) - 1))];
			const auto name = [&](const std::string &animation) { return animation.empty() ? 0u : game.templates.Model(animation); };
			const bool stop = set[2].size() == 4 && std::equal(set[2].begin(), set[2].end(), "STOP", [](char a, char b) {
				return std::toupper(static_cast<unsigned char>(a)) == b;
			});
			look.animations = {name(set[0]), name(set[1]), stop ? name(set[1]) : name(set[2])};
			look.finalStop = stop ? 1u : 0u;
			if (!nugget.finalEffect.empty())
				look.finalEffect = game.templates.PlayedEffect(gameplay::DeathEffectKind::Effect, nugget.finalEffect);
		}
		// ParticleSystem: a system attached to it (attachToObject), riding on it.
		if (!nugget.particleSystem.empty())
			look.particleSystem = game.templates.Model(nugget.particleSystem);
		world.Add<gameplay::DebrisLook>(entity);
		*world.Get<gameplay::DebrisLook>(entity) = look;
	}
	if (gameplay::Health *health = world.Get<gameplay::Health>(entity))
	{
		const Fixed share = Engine::Math::UniformFixed(random, nugget.minHealth, nugget.maxHealth);
		health->current = health->maximum * share;
	}

	// IgnorePrimaryObstacle (doStuffToObj: setIgnoreCollisionsWith): it passes through what made it.
	if (nugget.ignorePrimaryObstacle && game.world.IsAlive(source.entity))
		if (auto *response = world.Get<gameplay::BodyCollision>(entity))
			response->ignored = source.entity;
	// InvulnerableTime (Object::goInvulnerable): an undetected defector for that long, from now.
	if (nugget.invulnerableTicks > 0)
	{
		world.Add<gameplay::UndetectedDefector>(entity);
		world.Get<gameplay::UndetectedDefector>(entity)->until = game.tick + nugget.invulnerableTicks;
		if (auto *targetable = world.Get<gameplay::Targetable>(entity))
			targetable->classes |= gameplay::target_class::Undetected;
	}
	const std::uint32_t how = nugget.disposition;
	// Thrown, it makes its bounce sound each time it lands (PhysicsBehavior::setBounceSound).
	if ((how & (disposition::SendItFlying | disposition::SendItUp | disposition::RandomForce)) != 0 && !nugget.bounceSound.empty() &&
		world.Get<gameplay::PhysicsBody>(entity) != nullptr)
	{
		world.Add<gameplay::BounceSound>(entity);
		world.Get<gameplay::BounceSound>(entity)->sound = game.templates.PlayedEffect(gameplay::DeathEffectKind::Sound, nugget.bounceSound);
	}
	gameplay::Transform &transform = *world.Get<gameplay::Transform>(entity);
	const FixedVector3 at = source.position + Rotate(nugget.offset, source.facing);
	gameplay::PhysicsBody *body = world.Get<gameplay::PhysicsBody>(entity);
	gameplay::Attitude *attitude = world.Get<gameplay::Attitude>(entity);
	// INHERIT_VELOCITY: pushed by its source's velocity, as a force (divided by its own mass as it is).
	if ((how & disposition::InheritVelocity) != 0 && body != nullptr)
		gameplay::ApplyForce(*body, SourceVelocity(world, source.entity));
	if ((how & disposition::LikeExisting) != 0)
	{
		transform.facing = source.facing;
		transform.position = at;
		if (body != nullptr && source.position.z > game.ground.At(source.position.XY()))
			body->Set(gameplay::physics_flag::AllowToFall, true);
	}
	// ON_GROUND_ALIGNED: on the highest surface there (getHighestLayerForDestination: a bridge deck over the ground, 1 above
	// it for sloppy art), on that layer, turned at random.
	if ((how & disposition::OnGroundAligned) != 0)
	{
		transform.facing = AnyAngle(random);
		const auto *decks = world.FindResource<gameplay::DeckSurfaces>();
		const std::uint8_t layer = decks != nullptr ? gameplay::LayerForDestination(*decks, game.ground, {at.x, at.y, Fixed::FromInt(99999)}) : gameplay::GroundLayer;
		Fixed z = layer != gameplay::GroundLayer ? gameplay::LayerHeight(*decks, game.ground, at.XY(), layer) + Fixed::One() : game.ground.At(at.XY());
		transform.position = {at.x, at.y, z};
		SetLayer(game, entity, layer);
	}
	if ((how & disposition::SendItOut) != 0)
	{
		transform.facing = AnyAngle(random);
		transform.position = {at.x, at.y, game.ground.At(at.XY())};
		if (body != nullptr)
		{
			if (debris && nugget.mass > Fixed{})
				body->mass = nugget.mass;
			body->extraFriction = nugget.extraFriction;
			const Fixed horizontal = Fixed::FromInt(4) * nugget.intensity;
			const FixedVector3 force{Engine::Math::UniformFixed(random, -horizontal, horizontal), Engine::Math::UniformFixed(random, -horizontal, horizontal), Fixed{}};
			gameplay::ApplyForce(*body, force);
			if (nugget.orientInForceDirection)
				transform.facing = Engine::Math::Atan2(force.y, force.x);
		}
	}
	if ((how & (disposition::SendItFlying | disposition::SendItUp | disposition::RandomForce)) != 0)
	{
		transform.position = at;
		transform.facing = source.facing;
		if (body != nullptr)
		{
			if (debris && nugget.mass > Fixed{})
				body->mass = nugget.mass;
			body->extraFriction = nugget.extraFriction;
			body->Set(gameplay::physics_flag::AllowBouncing, true);
			// Unset rates: the spin from the intensity (pi/32 radians a tick per unit), the rest from the spin.
			const std::int32_t spin = nugget.spinRate ? *nugget.spinRate
				: static_cast<std::int32_t>((static_cast<std::int64_t>(Engine::Math::TurnAngle::Half_Turn().units / 32) * nugget.intensity.Raw()) >> Fixed::FractionBits);
			const std::int32_t yaw = Around(random, nugget.yawRate.value_or(spin));
			const std::int32_t roll = Around(random, nugget.rollRate.value_or(spin));
			const std::int32_t pitch = Around(random, nugget.pitchRate.value_or(spin));
			FixedVector3 force;
			if ((how & disposition::SendItFlying) != 0)
			{
				const Fixed horizontal = Fixed::FromInt(4) * nugget.intensity, vertical = Fixed::FromInt(3) * nugget.intensity;
				force = {Engine::Math::UniformFixed(random, -horizontal, horizontal), Engine::Math::UniformFixed(random, -horizontal, horizontal),
					Engine::Math::UniformFixed(random, vertical * Fixed::FromRatio(33, 100), vertical)};
			}
			else if ((how & disposition::SendItUp) != 0)
			{
				const Fixed horizontal = Fixed::FromInt(2) * nugget.intensity, vertical = Fixed::FromInt(4) * nugget.intensity;
				force = {Engine::Math::UniformFixed(random, -horizontal, horizontal), Engine::Math::UniformFixed(random, -horizontal, horizontal),
					Engine::Math::UniformFixed(random, vertical * Fixed::FromRatio(3, 4), vertical)};
			}
			else
				force = RandomForce(random, nugget.minForce, nugget.maxForce, nugget.minPitch, nugget.maxPitch);
			gameplay::ApplyForce(*body, force);
			if (nugget.orientInForceDirection)
				transform.facing = Engine::Math::Atan2(force.y, force.x);
			if (attitude != nullptr)
				*attitude = {};
			body->yawRate = yaw;
			body->rollRate = roll;
			body->pitchRate = pitch;
		}
	}
	if ((how & disposition::Whirling) != 0 && body != nullptr)
	{
		const std::int32_t rate = static_cast<std::int32_t>(Engine::Math::TurnFromRadians(nugget.intensity).units);
		body->yawRate = Around(random, rate);
		body->rollRate = Around(random, rate);
		body->pitchRate = Around(random, rate);
	}
}
}

// GenericObjectCreationNugget::doStuffToObj's DiesOnBadLand: under water, at most 10 above its surface, it takes huge water
// damage, dying FLOODED; else, off the map or on a cliff, water or impassable pathfinding cell, it is killed (Object::kill).
inline void DiesOnBadLand(GameWorld &game, ecs::Entity entity)
{
	namespace gp = engine::gameplay;
	using Engine::Math::Fixed;
	const auto *at = game.world.Get<gp::Transform>(entity);
	if (at == nullptr)
		return;
	const Engine::Math::FixedVector2 spot = at->position.XY();
	if (Fixed water; game.ground.Water(spot, water) && water > game.ground.At(spot) && at->position.z <= water + Fixed::FromInt(10))
	{
		DamageFrom(game, entity, {}, Fixed::FromInt(1000000), content::DamageTypeIndex("WATER").value_or(0), content::DeathTypeIndex("FLOODED").value_or(0));
		return;
	}
	const auto &grid = game.world.Resource<gp::NavigationGrid>();
	if (grid.Width() == 0)
		return;
	const auto cellX = static_cast<std::int32_t>((spot.x / Fixed::FromInt(gp::PathfindCellSize)).Floor());
	const auto cellY = static_cast<std::int32_t>((spot.y / Fixed::FromInt(gp::PathfindCellSize)).Floor());
	const gp::PathfindCellType type = grid.Contains(cellX, cellY) ? grid.Type(cellX, cellY) : gp::PathfindCellType::Impassable;
	if (!grid.Contains(cellX, cellY) || type == gp::PathfindCellType::Cliff || type == gp::PathfindCellType::Water || type == gp::PathfindCellType::Impassable)
		KillNow(game, entity);
}

// Returns the first object the list made (the original's ObjectCreationList::create; none: an empty entity).
ecs::Entity RunCreationList(GameWorld &game, std::string_view listName, const CreationSource &source)
{
	ecs::Entity first{};
	using namespace creation_detail;
	const auto &lists = game.templates.Content().creation;
	const auto found = lists.find(listName);
	if (found == lists.end())
		return first;
	const Fixed airborne = game.world.Resource<gameplay::PhysicsSettings>().SignificantHeight();
	for (const content::CreationNugget &nugget : found->second.nuggets)
	{
		if (nugget.kind == content::CreationKind::ApplyRandomForce)
		{
			ThrowSource(game, nugget, source);
			continue;
		}
		// FireWeaponNugget: WeaponStore::createAndFireTempWeapon from the source at the secondary point (the weapon system
		// fires it next tick). AttackNugget: the source's slot locked (setWeaponLock) and an attack on the secondary point,
		// NumberOfShots shots, from its own AI. Both need a source and a secondary point (not a death's list).
		if (nugget.kind == content::CreationKind::FireWeapon || nugget.kind == content::CreationKind::Attack)
		{
			if (!source.secondary || !game.world.IsAlive(source.entity))
				continue;
			if (nugget.kind == content::CreationKind::FireWeapon)
			{
				const std::uint32_t weapon = game.templates.Weapon(nugget.weapon);
				if (auto *fires = game.world.FindResource<gameplay::TemporaryWeaponFires>(); fires != nullptr && weapon != gameplay::WeaponCatalog::None)
				{
					// Fired by the source object from where it stands (not the list's primary point).
					const auto *owner = game.world.Get<gameplay::Owner>(source.entity);
					const auto *at = game.world.Get<gameplay::Transform>(source.entity);
					fires->Add({source.entity, weapon, owner != nullptr ? owner->player : 0u, at != nullptr ? at->position : source.position, *source.secondary});
				}
				continue;
			}
			if (auto *slots = game.world.Get<gameplay::WeaponSlots>(source.entity))
				if (auto *armament = game.world.Get<gameplay::Armament>(source.entity))
					gameplay::LockSlot(*slots, *armament, static_cast<std::uint8_t>(nugget.weaponSlot));
			OrderAttackPosition(game, source.entity, *source.secondary, nugget.shots, false);
			// Its RadiusDecalUpdate (if it has one) lays the DeliveryDecal at the target, until it is no longer attacking.
			if (game.world.Has<RadiusDecal>(source.entity))
				LayRadiusDecal(game, source.entity, nugget.deliveryDecal, nugget.deliveryDecalRadius, *source.secondary, RadiusDecalUntil::NoLongerAttacking);
			continue;
		}
		if (nugget.names.empty())
			continue;
		// RequiresLivePlayer: nothing for a source whose player is dead (killPlayer) or missing.
		if (nugget.requiresLivePlayer)
		{
			const std::uint32_t player = source.team < game.roster.TeamCount() ? game.roster.TeamAt(source.team).owner : 0xFFFFFFFFu;
			if (player >= game.roster.PlayerCount() || game.roster.PlayerAt(player).dead)
				continue;
			// VictoryConditions' killPlayer for a player beaten before this tick (one beaten by this tick's deaths is not
			// dead yet: the original's victory check comes after).
			if (const auto *outcome = game.world.FindResource<gameplay::MatchOutcome>())
				if (const auto *standing = outcome->Of(player); standing != nullptr && standing->defeated &&
					std::find(outcome->fallen.begin(), outcome->fallen.end(), player) == outcome->fallen.end())
					continue;
		}
		if (nugget.skipIfSignificantlyAirborne && source.position.z - game.ground.At(source.position.XY()) > airborne)
			continue;
		const bool debris = nugget.kind == content::CreationKind::CreateDebris;
		const std::uint32_t team = OwnerTeam(game, source.team);
		// PutInContainer: its container first, holding what it makes (ParachuteContain: one), thrown after them.
		ecs::Entity container{};
		if (!nugget.putInContainer.empty())
		{
			container = SpawnObject(game, nugget.putInContainer, source.position.XY(), source.facing, team, "");
			if (game.world.IsAlive(source.entity))
				SetProducer(game, container, source.entity);
		}
		if (first == ecs::Entity{} && game.world.IsAlive(container))
			first = container;
		// SpreadFormation's search sees the pieces already placed (the original's are in the partition as they are placed).
		std::vector<std::pair<Engine::Math::FixedVector2, Fixed>> placed;
		for (std::uint32_t piece = 0; piece < nugget.count; ++piece)
		{
			const auto pick = static_cast<std::size_t>(Engine::Math::UniformInt(game.random, 0, static_cast<std::int64_t>(nugget.names.size()) - 1));
			const std::string &name = nugget.names[pick];
			const ecs::Entity entity = SpawnObject(game, debris ? std::string("GenericDebris") : name, source.position.XY(), source.facing, team, "");
			if (!game.world.IsAlive(entity))
				continue;
			if (first == ecs::Entity{})
				first = entity;
			if (debris)
			{
				game.world.Add<gameplay::ModelOverride>(entity);
				*game.world.Get<gameplay::ModelOverride>(entity) = {game.templates.Model(name)};
			}
			// PreserveLayer: on its source's deck, when it is not put in a container.
			if (nugget.preserveLayer && !game.world.IsAlive(container) && game.world.IsAlive(source.entity))
				if (const auto *deck = game.world.Get<gameplay::SurfaceLayer>(source.entity); deck != nullptr && deck->layer != gameplay::GroundLayer)
					SetLayer(game, entity, deck->layer);
			if (game.world.IsAlive(container))
				PutInParachute(game, container, entity);
			if (nugget.spreadFormation)
			{
				// findPositionAround(center, minRadius A..B, MaxDistanceFormation, FPF_USE_HIGHEST_LAYER), its start angle at
				// random; a centre off the map is taken as it is; nothing found: the centre (the fork's deterministic fix).
				const Fixed nearest = Engine::Math::UniformFixed(game.random, nugget.minDistanceA, nugget.minDistanceB);
				CreationSource spread = source;
				if (InPathfindExtent(game, source.position.XY()))
				{
					const Engine::Math::TurnAngle start{static_cast<std::uint32_t>(Engine::Math::UniformInt(game.random, 0, 0xFFFFFFFFll))};
					const auto legal = SpotLegal(game);
					const auto spot = gameplay::FindPositionAround(source.position.XY(), nearest, nugget.maxDistance, start, [&](Engine::Math::FixedVector2 point) {
						for (const auto &[at, radius] : placed)
						{
							const Fixed apart = Fixed::FromInt(5) + radius;
							if (Engine::Math::DistanceSquared(point, at) < apart * apart)
								return false;
						}
						return legal(point);
					});
					if (spot)
						spread.position = {spot->x, spot->y, game.ground.At(*spot)};
				}
				Place(game, entity, nugget, spread, debris);
			}
			else
				Place(game, entity, nugget, source, debris);
			if (const auto *at = game.world.Get<gameplay::Transform>(entity))
			{
				const auto *ref = game.world.Get<gameplay::DefinitionRef>(entity);
				placed.emplace_back(at->position.XY(), ref != nullptr ? content::BoundingCircleRadius(game.templates.DefinitionAt(ref->index).geometry) : Fixed{});
			}
			// FadeIn / FadeOut: its drawable fades over FadeTime, FadeSound on the source.
			if (nugget.fadeIn || nugget.fadeOut)
				if (auto *cues = game.world.FindResource<EffectCues>())
					for (const bool in : {true, false})
						if (in ? nugget.fadeIn : nugget.fadeOut)
						{
							EffectCue cue;
							cue.effect = nugget.fadeSound;
							cue.at = source.position;
							cue.on = entity;
							cue.fade = in ? 1 : 2;
							cue.fadeTicks = nugget.fadeTicks;
							cues->list.push_back(std::move(cue));
						}
			// DiesOnBadLand (doStuffToObj): over water (on the ground, at most 10 above it) it drowns; on a cliff, water or
			// impassable cell, or off the map, it is killed.
			if (nugget.diesOnBadLand)
				DiesOnBadLand(game, entity);
			// GenericObjectCreationNugget::doStuffToObj / createDebris: setProducer(sourceObj).
			if (game.world.IsAlive(source.entity))
				SetProducer(game, entity, source.entity);
			// Object::setProducer: an EMP pulse knows who made it (its victim guides it: EMPUpdate::doDisableAttack).
			if (auto *pulse = game.world.Get<gameplay::EmpPulse>(entity); pulse != nullptr && game.world.IsAlive(source.entity))
				pulse->producer = source.entity;
			// ContainInsideSourceObject: inside the source if it will take it (a portable structure mounts on a carrier with
			// none yet); else stillborn (destroyObject).
			if (nugget.containInside)
			{
				if (!ContainInSource(game, source.entity, entity))
					RetireNow(game, {entity});
				continue;
			}
			// InheritsVeterancy: its source's level (an ejected pilot keeps the vehicle's rank).
			if (nugget.inheritsVeterancy)
				PlaceAtVeterancy(game, entity, source.veterancy);
			// "If they have a SlavedUpdate, then I have to tell them who their daddy is from now on."
			if (auto *slave = game.world.Get<gameplay::Slaved>(entity); slave != nullptr && game.world.IsAlive(source.entity))
			{
				const auto *master = game.world.Get<gameplay::DefinitionRef>(source.entity);
				const Fixed masterRadius = master != nullptr ? content::BoundingCircleRadius(game.templates.DefinitionAt(master->index).geometry) : Fixed{};
				gameplay::Enslave(*slave, source.entity, masterRadius, game.world.Resource<gameplay::RandomSeed>().value, game.tick, entity);
			}
		}
		if (game.world.IsAlive(container))
		{
			content::CreationNugget holder = nugget;
			holder.invulnerableTicks = 0;
			Place(game, container, holder, source, false);
		}
	}
	return first;
}
}
