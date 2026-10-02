export module games.generalszh.gameplay.railroad.systems.railroad_collision_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.railroad.components.railcar;
export import games.generalszh.gameplay.railroad.resources.rail_network;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.spatial.algorithms.geometry_collision;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.physics.components.physics_body;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.health.resources.incoming_damage;
export import engine.gameplay.common.health.systems.health_system;
export import engine.gameplay.common.status.components.status_flags;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.rts.collision.resources.contacts;
export import engine.gameplay.rts.construction.components.under_construction;
export import engine.gameplay.rts.mines.components.demo_trap;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.rts.containment.components.transport;
export import games.generalszh.gameplay.railroad.systems.railroad_system;
import games.generalszh.content.objects.object_status;
import Engine.Core.Math.FixedRandom;

// RailroadBehavior::onCollide for every train car touching something this tick (the partition's contacts at the end
// of the frame: a pair where one of them moved, their shapes touching (PartitionData::collidesWith), neither with
// NO_COLLISIONS nor NO_COLLIDE, not two immobile things), in entity order. A car in the wings or past the end of the line
// ignores it. Against another train's end (isRailroad: a car with a track, in play, leading or with nothing behind it) a
// locomotive kills it, a leading car kills it and itself; anything else that is a train does nothing. A faction
// structure (FS_POWER, FS_FACTORY, FS_BASE_DEFENSE, FS_TECHNOLOGY, a REBUILD_HOLE) it kills, whatever its speed; a demo
// trap kills it (once built) and is killed. What has no PhysicsBehavior it leaves be (a train car has none: its own
// train's cars never touch it), and so what is boarding it as it waits slower than its RunningGarrisonSpeedMax (its
// AI's enter target) and its own passengers. Anything else it shoves out by a quarter of their overlap (their
// major radii, plus 1; not infantry); only a locomotive not waiting at a station (and not coasting slower than its
// RunningGarrisonSpeedMax) goes on: from KillSpeedMin its victim is killed (spinning, 0.03 either way), slower it is
// crushed (s x 10); lifted to 2 above the ground, it takes the train's push (up to 1.4, 0.3 for the dead, times how
// head-on; a random lift 0.05 .. s / 10) unless infantry flying faster than 5, may fall, bounce and slide in the air, and
// hit from behind it spins (-0.06 s its sideways deviation). Damage joins the tick's; the pushes apply after it.
export namespace generalszh::gameplay
{
struct RailroadCollisionSystem
{
	using Query = ecs::Query<ecs::Read<Railcar>, ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<Railcar>,
		ecs::Read<engine::gameplay::PhysicsBody>, ecs::Read<engine::gameplay::Health>, ecs::Read<engine::gameplay::StatusFlags>,
		ecs::Read<engine::gameplay::UnderConstruction>, ecs::Read<engine::gameplay::DemoTrap>, ecs::Read<engine::gameplay::Locomotion>,
		ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::Boarding>, ecs::Read<engine::gameplay::Passenger>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::ColliderIndex>, ecs::Read<engine::gameplay::GroundHeight>,
		ecs::Read<engine::gameplay::RandomSeed>, ecs::Read<RailroadDamage>, ecs::Write<engine::gameplay::IncomingDamage>, ecs::Write<RailroadImpulses>,
		ecs::Write<RailroadCues>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		using Engine::Math::Fixed;
		using Engine::Math::FixedVector3;
		namespace gp = engine::gameplay;
		const auto &templates = context.Read<ObjectTemplates>();
		const auto &index = context.Read<gp::ColliderIndex>();
		const auto &ground = context.Read<gp::GroundHeight>();
		const auto &damage = context.Read<RailroadDamage>();
		auto &incoming = context.Write<gp::IncomingDamage>();
		auto &impulses = context.Write<RailroadImpulses>().impulses;
		impulses.clear();
		const auto lookup = context.Lookup<Lookup>();
		auto &cues = context.Write<RailroadCues>().list;
		const std::uint64_t tick = context.Tick();
		const std::uint64_t seed = context.Read<gp::RandomSeed>().value ^ 0x7A1Bu;
		const std::uint64_t noCollisions = std::uint64_t{1} << content::ObjectStatusBit("NO_COLLISIONS");
		const std::size_t structure = content::KindOfBit("STRUCTURE"), infantry = content::KindOfBit("INFANTRY"), immobile = content::KindOfBit("IMMOBILE"),
						  noCollide = content::KindOfBit("NO_COLLIDE");
		const std::array<std::size_t, 5> faction{content::KindOfBit("FS_POWER"), content::KindOfBit("FS_FACTORY"), content::KindOfBit("FS_BASE_DEFENSE"),
			content::KindOfBit("FS_TECHNOLOGY"), content::KindOfBit("REBUILD_HOLE")};
		const auto kindOf = [&](ecs::Entity entity) -> const content::ObjectDefinition * {
			const auto *reference = lookup.Get<gp::DefinitionRef>(entity);
			return reference != nullptr ? &templates.DefinitionAt(reference->index) : nullptr;
		};
		const auto bodyOf = [](const content::ObjectDefinition &kind, const gp::Transform &at) {
			const gp::BodyShape shape = kind.geometry.shape == content::GeometryShape::Box ? gp::BodyShape::Box
				: kind.geometry.shape == content::GeometryShape::Cylinder                  ? gp::BodyShape::Cylinder
																							: gp::BodyShape::Sphere;
			return gp::CollisionBody{at.position, at.facing, shape, kind.geometry.majorRadius, kind.geometry.minorRadius, kind.geometry.height};
		};
		const auto moving = [&](ecs::Entity entity) {
			if (const auto *body = lookup.Get<gp::PhysicsBody>(entity))
			{
				const Fixed tiny = Fixed::FromRatio(1, 1000);
				if (Engine::Math::Abs(body->velocity.x) > tiny || Engine::Math::Abs(body->velocity.y) > tiny || Engine::Math::Abs(body->velocity.z) > tiny)
					return true;
			}
			const auto *motion = lookup.Get<gp::Locomotion>(entity);
			return motion != nullptr && motion->speed > Fixed{};
		};
		// isRailroad.
		const auto railroad = [&](const Railcar &car) {
			return car.anchor != Railcar::NoTrack && car.wings == 0 && car.endOfLine == 0 && car.gone == 0 && (car.lead != 0 || car.trailer == ecs::Entity{});
		};
		// playImpactSound for what has no PhysicsBehavior (getBounceSound gives what has one a silent event): infantry's
		// MeatyBounceSound, a huge vehicle's or structure's BigMetalBounceSound, a vehicle's SmallMetalBounceSound, where it
		// is, at full volume (its speed and mass taken as the loudest), for its controller.
		const auto impact = [&](const RailroadConfig &config, ecs::Entity self, ecs::Entity victim, const content::ObjectDefinition &kind, const gp::Transform &at) {
			const std::string &sound = kind.Is("INFANTRY") ? config.meatySound : (kind.Is("HUGE_VEHICLE") || kind.Is("STRUCTURE")) ? config.bigMetalSound
				: kind.Is("VEHICLE") ? config.smallMetalSound : std::string{};
			if (sound.empty())
				return;
			const auto *owner = lookup.Get<gp::Owner>(victim);
			cues.push_back({RailroadCue::Kind::Impact, self, at.position, Fixed::One(), owner != nullptr ? owner->player : 0xFFFFFFFFu, sound});
		};
		const auto kill = [&](ecs::Entity victim, ecs::Entity source) {
			const auto *health = lookup.Get<gp::Health>(victim);
			if (health != nullptr)
				incoming.Add({victim, source, health->maximum, damage.unresistable, damage.normalDeath});
		};
		std::vector<std::pair<ecs::Entity, std::size_t>> cars;
		query.ForEachChunk([&](auto chunk) {
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < entities.size(); ++row)
				cars.emplace_back(entities[row], 0);
		});
		std::ranges::sort(cars, [](const auto &a, const auto &b) { return a.first.index != b.first.index ? a.first.index < b.first.index : a.first.generation < b.first.generation; });
		bool dealt = false;
		for (const auto &[self, unused] : cars)
		{
			(void)unused;
			const Railcar &car = *lookup.Get<Railcar>(self);
			const RailroadConfig *config = templates.RailroadOf(lookup.Get<gp::DefinitionRef>(self)->index);
			if (config == nullptr || car.anchor == Railcar::NoTrack || car.wings != 0 || car.endOfLine != 0 || car.gone != 0)
				continue;
			const gp::Transform &mine = *lookup.Get<gp::Transform>(self);
			const content::ObjectDefinition *ownKind = kindOf(self);
			if (ownKind == nullptr)
				continue;
			const gp::CollisionBody ownBody = bodyOf(*ownKind, mine);
			const bool moved = car.pull.speed != Fixed{} || car.conductor.speed != Fixed{};
			const Fixed speed = car.pull.speed;
			std::vector<ecs::Entity> touching;
			index.ForEachWithin(mine.position.XY(), config->radius + Fixed::FromInt(64), [&](const gp::SpatialEntry &entry) {
				if (entry.entity != self)
					touching.push_back(entry.entity);
			});
			std::ranges::sort(touching, [](ecs::Entity a, ecs::Entity b) { return a.index != b.index ? a.index < b.index : a.generation < b.generation; });
			for (const ecs::Entity other : touching)
			{
				const content::ObjectDefinition *kind = kindOf(other);
				const gp::Transform *theirs = lookup.Get<gp::Transform>(other);
				if (kind == nullptr || theirs == nullptr)
					continue;
				if (!moved && !moving(other))
					continue;
				if (content::HasKindOf(kind->kinds, noCollide) || content::HasKindOf(ownKind->kinds, noCollide) ||
					(content::HasKindOf(kind->kinds, immobile) && content::HasKindOf(ownKind->kinds, immobile)))
					continue;
				if (const auto *status = lookup.Get<gp::StatusFlags>(other); status != nullptr && (status->bits & noCollisions) != 0)
					continue;
				if (!gp::WouldCollide(ownBody, bodyOf(*kind, *theirs)))
					continue;
				// Another train.
				if (const Railcar *them = lookup.Get<Railcar>(other))
				{
					if (railroad(*them))
					{
						if (car.locomotive != 0)
							kill(other, self);
						else if (car.lead != 0)
						{
							kill(other, self);
							kill(self, self);
						}
						dealt = true;
					}
					continue; // no PhysicsBehavior: left be
				}
				if (content::HasKindOf(kind->kinds, structure))
				{
					if (std::ranges::any_of(faction, [&](std::size_t bit) { return content::HasKindOf(kind->kinds, bit); }))
					{
						if (lookup.Get<gp::PhysicsBody>(other) == nullptr)
							impact(*config, self, other, *kind, *theirs);
						kill(other, self);
						dealt = true;
						continue;
					}
					if (lookup.Get<gp::DemoTrap>(other) != nullptr)
					{
						if (lookup.Get<gp::UnderConstruction>(other) == nullptr)
							kill(self, self);
						if (lookup.Get<gp::PhysicsBody>(other) == nullptr)
							impact(*config, self, other, *kind, *theirs);
						kill(other, self);
						dealt = true;
						continue;
					}
				}
				const gp::PhysicsBody *physics = lookup.Get<gp::PhysicsBody>(other);
				if (physics == nullptr)
					continue;
				// Boarding it as it waits slowly enough (its AI's enter target): let through.
				if (car.state == ConductorState::WaitAtStation && speed < config->runningGarrisonSpeedMax)
					if (const auto *boarding = lookup.Get<gp::Boarding>(other); boarding != nullptr && boarding->transport == self)
						continue;
				// Its own passengers.
				if (const auto *passenger = lookup.Get<gp::Passenger>(other); passenger != nullptr && passenger->transport == self)
					continue;
				const bool soldier = content::HasKindOf(kind->kinds, infantry);
				RailroadImpulse push{other, theirs->position};
				// Its whistle for the disaster (a car has none).
				if (!config->whistleSound.empty())
					cues.push_back({RailroadCue::Kind::Whistle, self, mine.position, Fixed::One(), 0xFFFFFFFFu, config->whistleSound});
				// Shoved out of the way (not infantry): a quarter of the overlap of their major radii (plus 1), from its centre.
				FixedVector3 away = theirs->position - mine.position;
				const Fixed distance = Engine::Math::Length(away);
				const Fixed overlap = config->radius + kind->geometry.majorRadius - distance + Fixed::One();
				away = Engine::Math::Normalize(away);
				if (!soldier)
				{
					push.position = push.position + away * (overlap / Fixed::FromInt(4));
					push.setsPosition = 1;
				}
				const bool waits = car.state == ConductorState::WaitAtStation || (car.state == ConductorState::Coast && speed < config->runningGarrisonSpeedMax) ||
					car.locomotive == 0;
				if (waits)
				{
					if (push.setsPosition != 0)
						impulses.push_back(push);
					continue;
				}
				const Engine::Math::FixedVector2 facing = Engine::Math::Direction(mine.facing);
				FixedVector3 delta = Engine::Math::Normalize(push.position - mine.position);
				const Fixed dot = delta.x * facing.x + delta.y * facing.y;
				auto random = Engine::Math::Stream(seed, {tick, self.index, other.index, other.generation});
				const auto between = [&](Fixed low, Fixed high) { return low + (high - low) * Engine::Math::UniformFixed(random, Fixed{}, Fixed::One()); };
				const auto *health = lookup.Get<gp::Health>(other);
				if (health != nullptr && gp::IsDead(*health))
					delta = delta * std::min(Fixed::FromRatio(3, 10), speed * Fixed::FromRatio(66, 100));
				else
				{
					delta = delta * std::min(Fixed::FromRatio(14, 10), speed * Fixed::FromRatio(66, 100));
					if (speed >= config->killSpeedMin)
					{
						kill(other, self);
						const Fixed spin = Fixed::FromRatio(3, 100);
						push.pitchRate = static_cast<std::int32_t>(Engine::Math::TurnFromRadians(between(-spin, spin)).units);
						push.rollRate = static_cast<std::int32_t>(Engine::Math::TurnFromRadians(between(-spin, spin)).units);
						push.spins = 1;
					}
					else
						incoming.Add({other, self, speed * Fixed::FromInt(10), damage.crush, damage.crushedDeath});
					dealt = true;
				}
				// Lifted off the ground.
				push.position.z = std::max(push.position.z, ground.At(push.position.XY()) + Fixed::FromInt(2));
				push.setsPosition = 1;
				push.falls = 1;
				delta.z = between(Fixed::FromRatio(5, 100), speed / Fixed::FromInt(10));
				delta = delta * dot;
				const Fixed flying = Engine::Math::Length(physics->velocity);
				if (!(soldier && flying > Fixed::FromInt(5)))
				{
					push.velocity = delta;
					push.pushes = 1;
				}
				// setYawRate from behind: -0.06 s its deviation from the train's line (myDir x up).
				const FixedVector3 across{facing.y, -facing.x, Fixed{}};
				const FixedVector3 unit = Engine::Math::Normalize(delta);
				const Fixed deviation = across.x * unit.x + across.y * unit.y;
				if (dot > Fixed{})
				{
					push.yawRate = static_cast<std::int32_t>(
						Engine::Math::TurnFromRadians(deviation * Fixed::FromRatio(-6, 100) * speed).units);
					push.turns = 1;
				}
				impulses.push_back(push);
			}
		}
		if (dealt)
			incoming.Seal();
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::RailroadCollisionSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.railroad_collision";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// After the trains move, before the tick's damage is dealt.
	using Before = SystemTypeList<engine::gameplay::HealthSystem>;
	using After = SystemTypeList<generalszh::gameplay::RailroadSystem>;
};
}
