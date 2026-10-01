export module games.generalszh.gameplay.abilities.algorithms.special_objects;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.abilities.components.special_abilities;
export import games.generalszh.gameplay.abilities.components.special_object;
export import games.generalszh.gameplay.abilities.components.sticky_bomb;
export import games.generalszh.gameplay.abilities.systems.sticky_bomb_system;
export import games.generalszh.gameplay.abilities.resources.sticky_bomb_cues;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import games.generalszh.content.objects.object_status;
import games.generalszh.content.powers.special_ability_content;
import engine.gameplay.common.lifetime.components.lifetime;
import engine.gameplay.common.status.components.status_flags;
import engine.gameplay.common.identity.components.producer;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.rts.veterancy.components.experience;
import engine.gameplay.common.physics.components.physics_body;
import engine.gameplay.rts.combat.resources.shots;
import engine.gameplay.rts.lifecycle.resources.casualties;
import engine.gameplay.common.health.components.health;
import engine.gameplay.rts.death.components.dying;
import engine.ecs.query.query;
import Engine.Core.Math.FixedRandom;

// SpecialAbilityUpdate's special objects (createSpecialObject, killSpecialObjects, the count and the
// findSpecialObjectWithProducerID the command checks ask) and StickyBombUpdate's ends outside its system: a bomb stuck on
// its target (initStickyBomb), set off (detonate), a victim's booby trap set off (Object::checkAndDetonateBoobyTrap),
// the bombs the StickyBombSystem gave up on removed, and scripts' booby traps (doNamedSetBoobytrapped). A module's
// special objects are the SpecialObjects naming it, found with a chunked query (the original's ID list, validated).
export namespace generalszh::gameplay
{
namespace special_object_detail
{
namespace gp = engine::gameplay;
using Engine::Math::Fixed;

// BOOBY_TRAP_SCAN_RANGE.
inline constexpr std::int32_t BoobyTrapScanRange = 25;

inline std::uint64_t BoobyTrapped() noexcept { return std::uint64_t{1} << content::ObjectStatusBit("BOOBY_TRAPPED"); }

inline const content::ObjectDefinition *DefinitionOf(const GameWorld &game, ecs::Entity entity)
{
	const auto *ref = game.world.IsAlive(entity) ? game.world.Get<gp::DefinitionRef>(entity) : nullptr;
	return ref != nullptr ? &game.templates.DefinitionAt(ref->index) : nullptr;
}

inline void SetStatus(GameWorld &game, ecs::Entity entity, std::uint64_t mask, bool on)
{
	if (!game.world.IsAlive(entity))
		return;
	if (!game.world.Has<gp::StatusFlags>(entity))
	{
		if (!on)
			return;
		game.world.Add<gp::StatusFlags>(entity);
	}
	auto &bits = game.world.Get<gp::StatusFlags>(entity)->bits;
	bits = on ? (bits | mask) : (bits & ~mask);
}

inline bool HasStatus(const GameWorld &game, ecs::Entity entity, std::uint64_t mask)
{
	const auto *flags = game.world.IsAlive(entity) ? game.world.Get<gp::StatusFlags>(entity) : nullptr;
	return flags != nullptr && (flags->bits & mask) != 0;
}

inline std::uint32_t PlayerOf(const GameWorld &game, ecs::Entity entity)
{
	const auto *owner = game.world.IsAlive(entity) ? game.world.Get<gp::Owner>(entity) : nullptr;
	return owner != nullptr ? owner->player : 0u;
}

// The unit's SpecialAbilityUpdate module data for its slot (the module whose power the slot has).
inline std::optional<content::SpecialAbilityContent> ModuleOf(const GameWorld &game, ecs::Entity unit, std::uint8_t slot)
{
	const auto *abilities = game.world.IsAlive(unit) ? game.world.Get<SpecialAbilities>(unit) : nullptr;
	const content::ObjectDefinition *kind = DefinitionOf(game, unit);
	if (abilities == nullptr || kind == nullptr || slot >= abilities->count)
		return std::nullopt;
	const content::GameContent &content = game.templates.Content();
	for (const content::SpecialAbilityContent &module : content::ReadSpecialAbilities(*kind, game.step))
		if (const auto power = content.powers.Template(module.power); power && *power == abilities->slots[slot].power)
			return module;
	return std::nullopt;
}
}

// The module's special objects still in the world, oldest first (the order they were made: m_specialObjectIDList).
inline std::vector<ecs::Entity> SpecialObjectsOf(const GameWorld &game, ecs::Entity owner, std::uint8_t slot)
{
	std::vector<std::pair<std::uint32_t, ecs::Entity>> found;
	ecs::Query<ecs::Read<SpecialObject>> query(const_cast<ecs::World &>(game.world));
	query.ForEachChunk([&](auto chunk) {
		const auto objects = chunk.template Get<SpecialObject>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < objects.size(); ++row)
			if (objects[row].owner == owner && objects[row].slot == slot)
				found.emplace_back(objects[row].sequence, entities[row]);
	});
	std::ranges::sort(found, {}, &std::pair<std::uint32_t, ecs::Entity>::first);
	std::vector<ecs::Entity> out;
	for (const auto &[sequence, entity] : found)
		out.push_back(entity);
	return out;
}

// getSpecialObjectCount (validateSpecialObjects first: the gone ones are off the list).
inline std::uint32_t SpecialObjectCount(const GameWorld &game, ecs::Entity owner, std::uint8_t slot)
{
	return static_cast<std::uint32_t>(SpecialObjectsOf(game, owner, slot).size());
}

// findSpecialObjectWithProducerID: one of the module's special objects stuck on `target` (a sticky bomb's producer is
// what it is on).
inline bool HasSpecialObjectOn(const GameWorld &game, ecs::Entity owner, std::uint8_t slot, ecs::Entity target)
{
	for (const ecs::Entity object : SpecialObjectsOf(game, owner, slot))
		if (const auto *producer = game.world.Get<engine::gameplay::Producer>(object); producer != nullptr && producer->entity == target)
			return true;
	return false;
}

// killSpecialObjects: every one of the module's special objects destroyed (destroyObject).
inline void KillSpecialObjects(GameWorld &game, ecs::Entity owner, std::uint8_t slot)
{
	std::vector<ecs::Entity> objects = SpecialObjectsOf(game, owner, slot);
	if (!objects.empty())
		RetireNow(game, std::move(objects));
}

// The module slot of `unit` whose power is of `kind` (findSpecialAbilityUpdate: the first).
inline std::optional<std::uint8_t> SlotOfKind(const GameWorld &game, ecs::Entity unit, AbilityKind kind)
{
	const auto *abilities = game.world.IsAlive(unit) ? game.world.Get<SpecialAbilities>(unit) : nullptr;
	if (abilities != nullptr)
		for (std::uint8_t index = 0; index < abilities->count; ++index)
			if (abilities->slots[index].kind == kind)
				return index;
	return std::nullopt;
}

// createSpecialObject: at MaxSpecialObjects a persistent module makes no more, another's are destroyed first; its
// SpecialObject is made on the unit's team where it stands, facing its way, its experience going to the unit; with a
// body, it pitches at its CenterOfMassOffset (radians a frame: a Helix's NukeBomb tips as it falls) and has no
// airborne friction (setAllowAirborneFriction(false)).
inline ecs::Entity CreateSpecialObject(GameWorld &game, ecs::Entity unit, std::uint8_t slot)
{
	namespace gp = engine::gameplay;
	using namespace special_object_detail;
	const auto module = ModuleOf(game, unit, slot);
	if (!module)
		return {};
	if (SpecialObjectCount(game, unit, slot) == module->maxSpecialObjects)
	{
		if (module->specialObjectsPersistent)
			return {};
		KillSpecialObjects(game, unit, slot);
	}
	if (game.templates.Content().objects.Find(module->specialObject) == nullptr)
		return {};
	const gp::Transform at = *game.world.Get<gp::Transform>(unit);
	const auto *member = game.world.Get<gp::TeamMember>(unit);
	const ecs::Entity made = SpawnObject(game, module->specialObject, at.position.XY(), at.facing, member != nullptr ? member->team : 0u, "");
	if (!game.world.IsAlive(made))
		return {};
	game.world.Get<gp::Transform>(made)->position = at.position;
	game.world.Add<SpecialObject>(made);
	AbilitySlot &own = game.world.Get<SpecialAbilities>(unit)->slots[slot];
	*game.world.Get<SpecialObject>(made) = SpecialObject{unit, own.objectsMade++, slot,
		static_cast<std::uint8_t>(!module->specialObjectsPersistent || !module->specialObjectsPersistWhenOwnerDies ? 1 : 0)};
	if (auto *experience = game.world.Get<gp::Experience>(made))
		experience->sink = unit;
	if (auto *body = game.world.Get<gp::PhysicsBody>(made))
	{
		body->pitchRate = static_cast<std::int32_t>(Engine::Math::TurnFromRadians(body->centerOfMassOffset).units);
		body->Set(gp::physics_flag::AirborneFriction, false);
	}
	return made;
}

// StickyBombUpdate::initStickyBomb: stuck on `target` (its producer from now on); a timed bomb (a LifetimeUpdate) pings
// on the whole seconds before it dies, a remote one a second from now and every second after. On the target: at
// `specificPos` on the ground; on something immobile a bomber placed it on, where the bomber stands, on the ground
// (for mine clearers to reach); else OffsetZ above the target. A booby trap marks its victim BOOBY_TRAPPED.
inline void InitStickyBomb(GameWorld &game, ecs::Entity bomb, ecs::Entity target, ecs::Entity bomber = {},
	std::optional<Engine::Math::FixedVector3> specificPos = std::nullopt)
{
	namespace gp = engine::gameplay;
	using namespace special_object_detail;
	auto &world = game.world;
	const content::ObjectDefinition *kind = DefinitionOf(game, bomb);
	if (kind == nullptr)
		return;
	const bool targetAlive = target != ecs::Entity{} && world.IsAlive(target);
	// The structural changes first (a component added moves the bomb's row).
	if (!world.Has<StickyBomb>(bomb))
		world.Add<StickyBomb>(bomb);
	SetProducer(game, bomb, targetAlive ? target : ecs::Entity{});
	StickyBomb &state = *world.Get<StickyBomb>(bomb);
	state.target = targetAlive ? target : ecs::Entity{};
	const std::uint64_t now = game.tick;
	const std::uint64_t second = game.step.TicksPerSecond();
	if (const auto *lifetime = world.Get<gp::Lifetime>(bomb); lifetime != nullptr && lifetime->deletes == 0)
	{
		state.dieTick = lifetime->expiresTick;
		const std::uint64_t pings = (state.dieTick - now) / second;
		state.nextPingTick = state.dieTick - pings * second;
	}
	else
	{
		state.dieTick = 0;
		state.nextPingTick = now + second;
	}
	if (!targetAlive)
		return;
	const StickyBombConfig *config = game.templates.StickyBombOf(world.Get<gp::DefinitionRef>(bomb)->index);
	const content::ObjectDefinition *victim = DefinitionOf(game, target);
	Engine::Math::FixedVector3 pos = world.Get<gp::Transform>(target)->position;
	if (specificPos)
	{
		pos = *specificPos;
		pos.z = game.ground.At(pos.XY());
	}
	else if (victim != nullptr && victim->Is("IMMOBILE") && bomber != ecs::Entity{} && world.IsAlive(bomber))
	{
		pos = world.Get<gp::Transform>(bomber)->position;
		pos.z = game.ground.At(pos.XY());
	}
	else
		pos.z += config != nullptr ? config->offsetZ : Fixed::FromInt(10);
	world.Get<gp::Transform>(bomb)->position = pos;
	if (kind->Is("BOOBY_TRAP"))
		SetStatus(game, target, BoobyTrapped(), true);
	if (auto *cues = world.FindResource<StickyBombCues>())
		cues->list.push_back({StickyBombCue::Kind::Created, bomb, world.Get<gp::DefinitionRef>(bomb)->index, 0u, PlayerOf(game, bomb), pos, {}});
}

// StickyBombUpdate::detonate: with a GeometryBasedDamageWeapon, a blast round the thing it is on, its radii grown by
// that thing's bounding circle (primary damage within the primary radius of its edge, secondary within the secondary),
// and its GeometryBasedDamageFX there over the secondary radius; a booby trap's victim is BOOBY_TRAPPED no more; then
// the bomb is killed (its death modules fire its weapon).
inline void DetonateStickyBomb(GameWorld &game, ecs::Entity bomb)
{
	namespace gp = engine::gameplay;
	using namespace special_object_detail;
	auto &world = game.world;
	const content::ObjectDefinition *kind = DefinitionOf(game, bomb);
	if (kind == nullptr)
		return;
	const StickyBombConfig *config = game.templates.StickyBombOf(world.Get<gp::DefinitionRef>(bomb)->index);
	const auto *state = world.Get<StickyBomb>(bomb);
	const ecs::Entity trapped = state != nullptr && world.IsAlive(state->target) ? state->target : ecs::Entity{};
	if (config != nullptr && config->weapon != StickyBombConfig::None && trapped != ecs::Entity{})
	{
		const content::ObjectDefinition *victim = DefinitionOf(game, trapped);
		const Fixed circle = victim != nullptr ? content::BoundingCircleRadius(victim->geometry) : Fixed{};
		const Engine::Math::FixedVector3 at = world.Get<gp::Transform>(trapped)->position;
		gp::Shot blast{bomb, {}, config->weapon, PlayerOf(game, bomb), at, at, game.tick, game.tick + 1};
		blast.radiusBonus = circle;
		world.Resource<gp::ShotQueue>().Add(blast);
		if (config->effect != StickyBombConfig::None)
			if (auto *cues = world.FindResource<StickyBombCues>())
			{
				const Fixed secondary = world.Resource<gp::WeaponCatalog>().At(config->weapon).secondaryRadius + circle;
				cues->list.push_back({StickyBombCue::Kind::Effect, bomb, world.Get<gp::DefinitionRef>(bomb)->index, config->effect, PlayerOf(game, bomb), at, secondary});
			}
	}
	if (kind->Is("BOOBY_TRAP") && trapped != ecs::Entity{})
		SetStatus(game, trapped, BoobyTrapped(), false);
	KillNow(game, bomb);
}

// Object::checkAndDetonateBoobyTrap for something that is BOOBY_TRAPPED (`knownTrapped`: its status, when it is still
// here to ask): the nearest BOOBY_TRAP on the map as it is (by centre, in 2D) within BOOBY_TRAP_SCAN_RANGE of its
// bounding circle that is stuck on it goes off, unless `victim` is its player's ally. Whether one went off.
inline bool CheckAndDetonateBoobyTrap(GameWorld &game, ecs::Entity trapped, Engine::Math::FixedVector3 position, Fixed circle, bool offMap,
	ecs::Entity victim = {})
{
	namespace gp = engine::gameplay;
	using namespace special_object_detail;
	auto &world = game.world;
	const Fixed range = Fixed::FromInt(BoobyTrapScanRange) + circle;
	ecs::Entity nearest;
	Fixed best;
	// (The original's search also finds a spent trap, killed and still stuck on it, and sets that off again: a quirk
	// fixed, the dead ones passed over.)
	ecs::Query<ecs::Read<StickyBomb>, ecs::Read<gp::Transform>, ecs::Read<gp::DefinitionRef>, ecs::Optional<gp::OffMap>, ecs::Optional<gp::Health>,
		ecs::Exclude<gp::Dying>>
		query(world);
	query.ForEachChunk([&](auto chunk) {
		if (chunk.template Get<gp::OffMap>().empty() == offMap)
			return;
		const auto healths = chunk.template Get<gp::Health>();
		const auto bombs = chunk.template Get<StickyBomb>();
		const auto transforms = chunk.template Get<gp::Transform>();
		const auto refs = chunk.template Get<gp::DefinitionRef>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < bombs.size(); ++row)
		{
			if (bombs[row].target != trapped || !game.templates.DefinitionAt(refs[row].index).Is("BOOBY_TRAP") || (!healths.empty() && gp::IsDead(healths[row])))
				continue;
			const Fixed distance = Engine::Math::DistanceSquared(transforms[row].position.XY(), position.XY());
			if (distance > range * range)
				continue;
			if (nearest == ecs::Entity{} || distance < best || (distance == best && entities[row].index < nearest.index))
			{
				nearest = entities[row];
				best = distance;
			}
		}
	});
	if (nearest == ecs::Entity{})
		return false;
	if (victim != ecs::Entity{} && world.IsAlive(victim))
		if (const auto *relationships = world.FindResource<gp::Relationships>())
		{
			const auto *team = world.Get<gp::TeamMember>(victim);
			if (relationships->Allies(gp::Relationships::NoTeam, PlayerOf(game, nearest), team != nullptr ? team->team : gp::Relationships::NoTeam,
					PlayerOf(game, victim)))
				return false;
		}
	DetonateStickyBomb(game, nearest);
	return true;
}

// Likewise for something in the world: not BOOBY_TRAPPED, nothing.
inline bool CheckAndDetonateBoobyTrap(GameWorld &game, ecs::Entity trapped, ecs::Entity victim = {})
{
	namespace gp = engine::gameplay;
	using namespace special_object_detail;
	if (!HasStatus(game, trapped, BoobyTrapped()))
		return false;
	const content::ObjectDefinition *kind = DefinitionOf(game, trapped);
	return CheckAndDetonateBoobyTrap(game, trapped, game.world.Get<gp::Transform>(trapped)->position,
		kind != nullptr ? content::BoundingCircleRadius(kind->geometry) : Fixed{}, game.world.Get<gp::OffMap>(trapped) != nullptr, victim);
}

// doNamedSetBoobytrapped / doTeamSetBoobytrapped: a `type` made on the unit's team; with a StickyBombUpdate it is stuck on
// the unit at a random point of its box's edge (a sphere or cylinder: its middle), turned and moved as the unit is.
inline void SetBoobytrapped(GameWorld &game, const std::string &type, ecs::Entity unit)
{
	namespace gp = engine::gameplay;
	using namespace special_object_detail;
	auto &world = game.world;
	const content::ObjectDefinition *kind = DefinitionOf(game, unit);
	if (kind == nullptr || game.templates.Content().objects.Find(type) == nullptr)
		return;
	const auto *member = world.Get<gp::TeamMember>(unit);
	const ecs::Entity trap = SpawnObject(game, type, {}, {}, member != nullptr ? member->team : 0u, "");
	if (!world.IsAlive(trap) || game.templates.StickyBombOf(world.Get<gp::DefinitionRef>(trap)->index) == nullptr)
		return;
	// GeometryInfo::makeRandomOffsetOnPerimeter.
	Engine::Math::FixedVector2 offset;
	if (kind->geometry.shape == content::GeometryShape::Box)
	{
		const Fixed half = Fixed::FromRatio(1, 2);
		const Fixed major = kind->geometry.majorRadius, minor = kind->geometry.minorRadius;
		if (Engine::Math::UniformFixed(game.random, Fixed{}, Fixed::One()) < half)
		{
			offset.x = Engine::Math::UniformFixed(game.random, -major, major);
			offset.y = Engine::Math::UniformFixed(game.random, Fixed{}, Fixed::One()) < half ? -minor : minor;
		}
		else
		{
			offset.y = Engine::Math::UniformFixed(game.random, -minor, minor);
			offset.x = Engine::Math::UniformFixed(game.random, Fixed{}, Fixed::One()) < half ? -major : major;
		}
	}
	const gp::Transform &at = *world.Get<gp::Transform>(unit);
	const Fixed c = Engine::Math::Cos(at.facing), s = Engine::Math::Sin(at.facing);
	const Engine::Math::FixedVector3 pos{at.position.x + offset.x * c - offset.y * s, at.position.y + offset.x * s + offset.y * c, at.position.z};
	InitStickyBomb(game, trap, unit, {}, pos);
}

// The StickyBombSystem's tick: bombs whose target died destroyed (onDelete: a booby trap's victim BOOBY_TRAPPED no
// more), pings for the presentation.
inline void ApplyStickyBombEvents(GameWorld &game)
{
	namespace gp = engine::gameplay;
	using namespace special_object_detail;
	auto *resource = game.world.FindResource<StickyBombEvents>();
	if (resource == nullptr)
		return;
	std::vector<StickyBombEvent> events;
	resource->AppendTo(events);
	resource->Reset(0);
	std::vector<ecs::Entity> gone;
	for (const StickyBombEvent &event : events)
	{
		if (!game.world.IsAlive(event.bomb))
			continue;
		if (event.kind == StickyBombEvent::Kind::Destroy)
		{
			if (const content::ObjectDefinition *kind = DefinitionOf(game, event.bomb); kind != nullptr && kind->Is("BOOBY_TRAP"))
				SetStatus(game, game.world.Get<StickyBomb>(event.bomb)->target, BoobyTrapped(), false);
			gone.push_back(event.bomb);
		}
		else if (auto *cues = game.world.FindResource<StickyBombCues>())
			cues->list.push_back({StickyBombCue::Kind::Ping, event.bomb, game.world.Get<gp::DefinitionRef>(event.bomb)->index, 0u, PlayerOf(game, event.bomb),
				game.world.Get<gp::Transform>(event.bomb)->position, {}});
	}
	if (!gone.empty())
		RetireNow(game, std::move(gone));
}

// OpenContain::addToContain's checkAndDetonateBoobyTrap(rider): the tick's boardings set off their containers' booby
// traps. (The original checked before letting the rider in, and kept it out when the blast killed either: here the
// blast lands with the next tick's, the rider inside.)
inline void ApplyBoobyTrapEntries(GameWorld &game)
{
	std::vector<std::pair<ecs::Entity, ecs::Entity>> entries;
	for (const engine::gameplay::CargoChange &change : game.manifest.Changes())
		if (change.entered)
			entries.emplace_back(change.container, change.rider);
	for (const auto &[container, rider] : entries)
		if (game.world.IsAlive(container) && game.world.IsAlive(rider))
			CheckAndDetonateBoobyTrap(game, container, rider);
}

// The tick's casualties: Object::onDie's checkAndDetonateBoobyTrap(nullptr) for each thing killed (its booby trap goes
// off); and SpecialAbilityUpdate::onExit(TRUE) as a unit dies or goes, its special objects that do not outlive it
// destroyed.
inline void ApplySpecialObjectCasualties(GameWorld &game, std::span<const engine::gameplay::Casualty> casualties)
{
	namespace gp = engine::gameplay;
	if (casualties.empty())
		return;
	bool anyBombs = false, anyObjects = false;
	{
		ecs::Query<ecs::Read<StickyBomb>> bombs(game.world);
		bombs.ForEachChunk([&](auto chunk) { anyBombs = anyBombs || chunk.Entities().size() > 0; });
		ecs::Query<ecs::Read<SpecialObject>> objects(game.world);
		objects.ForEachChunk([&](auto chunk) { anyObjects = anyObjects || chunk.Entities().size() > 0; });
	}
	for (const gp::Casualty &casualty : casualties)
	{
		if (anyBombs && casualty.departure == gp::Departure::Killed && casualty.definition < game.templates.DefinitionCount())
		{
			const content::ObjectDefinition &kind = game.templates.DefinitionAt(casualty.definition);
			const bool offMap = game.world.IsAlive(casualty.entity) && game.world.Get<gp::OffMap>(casualty.entity) != nullptr;
			CheckAndDetonateBoobyTrap(game, casualty.entity, casualty.transform.position, content::BoundingCircleRadius(kind.geometry), offMap);
		}
		if (!anyObjects)
			continue;
		std::vector<ecs::Entity> gone;
		ecs::Query<ecs::Read<SpecialObject>> query(game.world);
		query.ForEachChunk([&](auto chunk) {
			const auto objects = chunk.template Get<SpecialObject>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < objects.size(); ++row)
				if (objects[row].owner == casualty.entity && objects[row].killWithOwner != 0)
					gone.push_back(entities[row]);
		});
		if (!gone.empty())
			RetireNow(game, std::move(gone));
	}
}
}
