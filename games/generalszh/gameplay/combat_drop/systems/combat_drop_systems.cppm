export module games.generalszh.gameplay.combat_drop.systems.combat_drop_systems;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.combat_drop.components.combat_drop;
export import games.generalszh.gameplay.combat_drop.algorithms.combat_drop_orders;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.surface_layer;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.spatial.resources.deck_surfaces;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.health.components.inactive_body;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.common.physics.components.physics_body;
export import engine.gameplay.common.physics.resources.physics_settings;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.common.lifetime.components.lifetime;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.rts.containment.resources.cargo_manifest;
export import engine.gameplay.rts.harvesting.components.harvester;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.navigation.components.navigation;
import Engine.Core.Math.FixedRandom;

// The combat drop's systems, each tick before physics (the original's AI updates run before its physics), in order:
//   RopeHangerSystem (removeDoneRappellers): a rappeller on a rope stays on it while it is alive and above the ground;
//     its transport dead, the drop is over for it (ChinookCombatDropState::onExit: aiIdle): it falls on its own. The
//     ropes still in use this tick go to RopesInUse.
//   CombatDropSystem (ChinookAIUpdate's MOVE_TO_COMBAT_DROP and DO_COMBAT_DROP states):
//     TakingOff: once up, it heads for its goal (ChinookMoveToBldgState::onEnter).
//     Moving: flying for its goal object wherever it is (else the spot); its move over, it drops once within 3 of the
//       height it wants over the ground there (while not, it keeps on). Another order ends it (its height back).
//     Dropping (ChinookCombatDropState): onEnter: DISABLED_HELD (it hangs where it is: its height held there), every
//       supply box lost, and a rope from each RopeStart bone (at most NumRopes and as many as it has RopeEnd bones; none:
//       the drop fails), each out to RopeFinalHeight over the ground below it, its first rappeller after a random
//       0 to PerRopeDelayMax - PerRopeDelayMin ticks (one draw per rope, in order). Each tick, rope by rope: one still
//       unrolling speeds up by gravity (to RopeDropSpeed at most) and lengthens (past its full length on its last step),
//       and with WaitForRopesToDrop its next rappeller waits a tick more; else when its time has come the first rider
//       that can rappel (in the order they got in) goes out through the door and is put at the rope's end
//       (setWorldTransform), rappelling onto the goal object (or the goal's ground), and the rope waits a random
//       PerRopeDelayMin to PerRopeDelayMax ticks. A rope with someone on it is in use; none in use and nobody left who can
//       rappel, the drop succeeds. It fails when the transport is dead. onExit: no longer held, its locomotor's height
//       back, each rope let go: it falls away for 150 ticks (setRopeSpeed, setExpirationDate).
//   RappelSystem (AIRappelState::update): a rappeller comes straight down (no drift; no faster than its rappel rate),
//     the building gone it drops to the ground below; at the height it stops at it lands (RappelLandings, applied
//     after the step) and physics lets its locomotor have it again.
export namespace generalszh::gameplay
{
namespace combat_drop_systems_detail
{
namespace gp = engine::gameplay;

inline bool Dead(const gp::Health *health, const gp::Dying *dying) { return dying != nullptr || (health != nullptr && gp::IsDead(*health)); }

// Thing::transformBoneToWorld for a bone at rest: turned with it, from where it stands.
inline Engine::Math::FixedVector3 ToWorld(const gp::Transform &at, const Engine::Math::FixedVector3 &local)
{
	const Engine::Math::Fixed c = Engine::Math::Cos(at.facing), s = Engine::Math::Sin(at.facing);
	return {at.position.x + local.x * c - local.y * s, at.position.y + local.x * s + local.y * c, at.position.z + local.z};
}
}

struct RopeHangerSystem
{
	using Query = ecs::Query<ecs::Read<OnRope>, ecs::Read<engine::gameplay::Transform>, ecs::Optional<engine::gameplay::Health>,
		ecs::Optional<engine::gameplay::Dying>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Health>, ecs::Read<engine::gameplay::Dying>, ecs::Read<CombatDrop>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::GroundHeight>, ecs::Write<RopesInUse>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<RopesInUse>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using namespace combat_drop_systems_detail;
		const gp::GroundHeight &ground = context.Read<gp::GroundHeight>();
		auto &out = context.Write<RopesInUse>().Slot(context);
		auto &commands = context.Commands();
		const auto lookup = context.Lookup<Lookup>();
		const auto ropes = chunk.Get<OnRope>();
		const auto transforms = chunk.Get<gp::Transform>();
		const auto healths = chunk.Get<gp::Health>();
		const auto dyings = chunk.Get<gp::Dying>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < ropes.size(); ++row)
		{
			const ecs::Entity transport = ropes[row].transport;
			const bool carrierGone = !lookup.IsAlive(transport) || Dead(lookup.Get<gp::Health>(transport), lookup.Get<gp::Dying>(transport));
			if (carrierGone || lookup.Get<CombatDrop>(transport) == nullptr)
			{
				// Its transport's drop over (it died): aiIdle, its rappel given up; physics has it until it lands.
				commands.Remove<OnRope>(entities[row]);
				if (carrierGone)
					commands.Remove<Rappel>(entities[row]);
				continue;
			}
			const auto &at = transforms[row].position;
			const bool dead = Dead(healths.empty() ? nullptr : &healths[row], dyings.empty() ? nullptr : &dyings[row]);
			if (dead || at.z - ground.At(at.XY()) <= Engine::Math::Fixed{})
			{
				commands.Remove<OnRope>(entities[row]);
				continue;
			}
			out.push_back({transport, ropes[row].rope});
		}
	}
};

struct CombatDropSystem
{
	using Query = ecs::Query<ecs::Write<CombatDrop>, ecs::Write<engine::gameplay::MoveOrder>, ecs::Write<engine::gameplay::Locomotion>,
		ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::OptionalWrite<engine::gameplay::Transport>,
		ecs::OptionalWrite<engine::gameplay::Disabled>, ecs::OptionalWrite<engine::gameplay::Route>, ecs::OptionalWrite<engine::gameplay::Harvester>,
		ecs::Optional<engine::gameplay::Health>, ecs::Optional<engine::gameplay::Dying>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Health>,
		ecs::Read<engine::gameplay::Dying>, ecs::Read<engine::gameplay::InactiveBody>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::CargoManifest>, ecs::Read<engine::gameplay::GroundHeight>,
		ecs::Read<engine::gameplay::PhysicsSettings>, ecs::Read<engine::gameplay::RandomSeed>, ecs::Read<RopesInUse>, ecs::Write<engine::gameplay::PlacedExits>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const
	{
		context.Write<engine::gameplay::PlacedExits>().Reset(query.PreparedChunkCount());
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using namespace combat_drop_systems_detail;
		using Engine::Math::Fixed;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const gp::CargoManifest &manifest = context.Read<gp::CargoManifest>();
		const gp::GroundHeight &ground = context.Read<gp::GroundHeight>();
		const Fixed gravity = Engine::Math::Abs(context.Read<gp::PhysicsSettings>().gravity);
		const std::uint64_t seed = context.Read<gp::RandomSeed>().value ^ 0xC0DB0u;
		const RopesInUse &inUse = context.Read<RopesInUse>();
		auto &exits = context.Write<gp::PlacedExits>().Slot(context);
		auto &commands = context.Commands();
		const auto lookup = context.Lookup<Lookup>();
		auto drops = chunk.Get<CombatDrop>();
		auto orders = chunk.Get<gp::MoveOrder>();
		auto motions = chunk.Get<gp::Locomotion>();
		const auto transforms = chunk.Get<gp::Transform>();
		const auto definitions = chunk.Get<gp::DefinitionRef>();
		auto transports = chunk.Get<gp::Transport>();
		auto disabled = chunk.Get<gp::Disabled>();
		auto routes = chunk.Get<gp::Route>();
		auto harvesters = chunk.Get<gp::Harvester>();
		const auto healths = chunk.Get<gp::Health>();
		const auto dyings = chunk.Get<gp::Dying>();
		const auto entities = chunk.Entities();
		const std::uint64_t tick = context.Tick();
		const auto definitionOf = [&](ecs::Entity entity) -> const content::ObjectDefinition * {
			const auto *ref = lookup.Get<gp::DefinitionRef>(entity);
			return ref != nullptr ? &templates.DefinitionAt(ref->index) : nullptr;
		};
		const auto deadAt = [&](ecs::Entity entity) {
			return !lookup.IsAlive(entity) || lookup.Get<gp::InactiveBody>(entity) != nullptr || Dead(lookup.Get<gp::Health>(entity), lookup.Get<gp::Dying>(entity));
		};
		for (std::size_t row = 0; row < drops.size(); ++row)
		{
			CombatDrop &drop = drops[row];
			gp::MoveOrder &order = orders[row];
			gp::Locomotion &motion = motions[row];
			const gp::Transform &at = transforms[row];
			gp::Transport *transport = transports.empty() ? nullptr : &transports[row];
			gp::Route *route = routes.empty() ? nullptr : &routes[row];
			const ecs::Entity self = entities[row];
			const ObjectTemplates::CombatDropConfig *config = templates.CombatDropOf(definitions[row].index);
			// The drop over: held no more, its height back, its ropes let go.
			const auto end = [&] {
				if (drop.stage == CombatDropStage::Dropping)
				{
					if (!disabled.empty())
						disabled[row].mask &= ~gp::disabled_type::Held;
					for (std::size_t index = 0; index < drop.ropeCount; ++index)
					{
						const DropRope &rope = drop.ropes[index];
						const auto falling = commands.Create();
						commands.Add<FallingRope>(falling, FallingRope{rope.top, rope.length, rope.lengthMax, tick, definitions[row].index, 0});
						commands.Add<gp::Lifetime>(falling, gp::Lifetime{tick + 150, 1, 0});
					}
				}
				if (drop.stage != CombatDropStage::TakingOff)
					motion.locomotor.preferredHeight = drop.oldHeight;
				commands.Remove<CombatDrop>(self);
			};
			if (config == nullptr || Dead(healths.empty() ? nullptr : &healths[row], dyings.empty() ? nullptr : &dyings[row]))
			{
				end();
				continue;
			}
			const auto enterMove = [&] {
				const content::ObjectDefinition *building = drop.target != ecs::Entity{} ? definitionOf(drop.target) : nullptr;
				const bool structure = building != nullptr && building->Is("STRUCTURE") && !deadAt(drop.target);
				if (drop.target != ecs::Entity{} && !deadAt(drop.target))
					drop.goal = lookup.Get<gp::Transform>(drop.target)->position.XY();
				EnterMoveToCombatDrop(drop, motion, order, route, ground, structure, structure ? combat_drop_detail::MaxHeightAbove(*building) : Fixed{},
					config->content.minDropHeight);
			};
			switch (drop.stage)
			{
			case CombatDropStage::TakingOff:
				if (transport != nullptr && (transport->landing || transport->takingOff))
				{
					transport->landRequested = false;
					break;
				}
				enterMove();
				break;
			case CombatDropStage::Moving:
			{
				// AIMoveToState with a goal object: wherever it now is.
				if (drop.target != ecs::Entity{} && !deadAt(drop.target))
				{
					const auto now = lookup.Get<gp::Transform>(drop.target)->position.XY();
					if (now != drop.goal && order.mode == gp::MoveMode::Point && order.destination == drop.goal)
					{
						drop.goal = now;
						order = gp::MoveToPoint(now);
						if (route != nullptr)
							route->planned = false;
					}
				}
				if (order.mode == gp::MoveMode::Point && order.destination == drop.goal)
					break;
				if (order.mode != gp::MoveMode::Idle)
				{
					end();
					break;
				}
				if (Engine::Math::Abs(at.position.z - drop.destZ) > Fixed::FromInt(3))
					break;
				// ChinookCombatDropState::onEnter.
				const std::size_t count = std::min<std::size_t>({config->content.numRopes, config->ropeStarts.size(), config->ropeEnds.size(), CombatDrop::MaxRopes});
				if (count == 0)
				{
					end();
					break;
				}
				if (!disabled.empty())
					disabled[row].mask |= gp::disabled_type::Held;
				else
					commands.Add<gp::Disabled>(self, gp::Disabled{gp::disabled_type::Held});
				// Held, it hangs where it is (its physics stops): its locomotor holds this height until the drop is over.
				motion.locomotor.preferredHeight = at.position.z - ground.Surface(at.position.XY());
				motion.speed = {};
				order = gp::MoveOrder{};
				if (!harvesters.empty())
					harvesters[row].boxes = 0;
				auto random = Engine::Math::Stream(seed, {tick, self.index, self.generation});
				const std::uint64_t low = config->content.perRopeDelayMin, high = std::max(config->content.perRopeDelayMin, config->content.perRopeDelayMax);
				for (std::size_t index = 0; index < count; ++index)
				{
					DropRope &rope = drop.ropes[index];
					rope = DropRope{};
					rope.top = ToWorld(at, config->ropeStarts[index]);
					rope.dropAt = ToWorld(at, config->ropeEnds[index].position);
					rope.dropFacing = at.facing + config->ropeEnds[index].facing;
					rope.length = Fixed::One();
					rope.lengthMax = rope.top.z - ground.At(rope.top.XY()) - config->content.ropeFinalHeight;
					rope.nextDrop = tick + static_cast<std::uint64_t>(Engine::Math::UniformInt(random, static_cast<std::int64_t>(low), static_cast<std::int64_t>(high))) - low;
				}
				drop.ropeCount = static_cast<std::uint8_t>(count);
				drop.stage = CombatDropStage::Dropping;
				break;
			}
			case CombatDropStage::Dropping:
			{
				// getPotentialRappeller: the first rider aboard that can rappel (not already out this tick).
				std::vector<ecs::Entity> taken;
				const auto potential = [&]() -> ecs::Entity {
					for (const ecs::Entity rider : manifest.Aboard(self))
					{
						if (std::find(taken.begin(), taken.end(), rider) != taken.end())
							continue;
						if (const content::ObjectDefinition *kind = definitionOf(rider); kind != nullptr && kind->Is("CAN_RAPPEL"))
							return rider;
					}
					return {};
				};
				auto random = Engine::Math::Stream(seed, {tick, self.index, self.generation, 1});
				const std::uint64_t low = config->content.perRopeDelayMin, high = std::max(config->content.perRopeDelayMin, config->content.perRopeDelayMax);
				const content::ObjectDefinition *building = drop.target != ecs::Entity{} ? definitionOf(drop.target) : nullptr;
				const bool structure = building != nullptr && building->Is("STRUCTURE") && !deadAt(drop.target);
				std::uint32_t ropesInUse = 0;
				for (std::uint32_t index = 0; index < drop.ropeCount; ++index)
				{
					DropRope &rope = drop.ropes[index];
					if (rope.length < rope.lengthMax)
					{
						rope.speed = std::min(rope.speed + gravity, config->content.ropeDropSpeed);
						rope.length += rope.speed;
						if (config->content.waitForRopesToDrop)
						{
							++rope.nextDrop;
							continue;
						}
					}
					bool used = false;
					inUse.ForEach([&](const RopeInUse &hanging) { used = used || (hanging.transport == self && hanging.rope == index); });
					if (tick >= rope.nextDrop)
						if (const ecs::Entity rider = potential(); rider != ecs::Entity{})
						{
							taken.push_back(rider);
							exits.push_back({self, rider, rope.dropAt, rope.dropFacing});
							// AIRappelState::onEnter: down to the ground below (or onto the building's top).
							Rappel rappel;
							rappel.building = structure ? drop.target : ecs::Entity{};
							rappel.destZ = ground.At(rope.dropAt.XY()) + (structure ? combat_drop_detail::MaxHeightAbove(*building) : Fixed{});
							rappel.rate = Fixed{} - std::min(config->content.rappelSpeed, gravity * Fixed::FromInt(30) * Fixed::FromRatio(5, 2));
							commands.Add<Rappel>(rider, rappel);
							commands.Add<OnRope>(rider, OnRope{self, index, 0});
							rope.nextDrop = tick + static_cast<std::uint64_t>(Engine::Math::UniformInt(random, static_cast<std::int64_t>(low), static_cast<std::int64_t>(high)));
							used = true;
						}
					if (used)
						++ropesInUse;
				}
				if (ropesInUse == 0 && potential() == ecs::Entity{})
					end();
				break;
			}
			}
		}
	}
};

struct RappelSystem
{
	using Query = ecs::Query<ecs::Write<Rappel>, ecs::Write<engine::gameplay::Transform>, ecs::Write<engine::gameplay::PhysicsBody>,
		ecs::Optional<engine::gameplay::SurfaceLayer>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Health>, ecs::Read<engine::gameplay::Dying>, ecs::Read<engine::gameplay::InactiveBody>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::GroundHeight>, ecs::Read<engine::gameplay::DeckSurfaces>, ecs::Write<RappelLandings>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<RappelLandings>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using namespace combat_drop_systems_detail;
		const gp::GroundHeight &ground = context.Read<gp::GroundHeight>();
		const gp::DeckSurfaces &decks = context.Read<gp::DeckSurfaces>();
		auto &out = context.Write<RappelLandings>().Slot(context);
		auto &commands = context.Commands();
		const auto lookup = context.Lookup<Lookup>();
		auto rappels = chunk.Get<Rappel>();
		auto transforms = chunk.Get<gp::Transform>();
		auto bodies = chunk.Get<gp::PhysicsBody>();
		const auto layers = chunk.Get<gp::SurfaceLayer>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < rappels.size(); ++row)
		{
			Rappel &rappel = rappels[row];
			auto &position = transforms[row].position;
			gp::PhysicsBody &body = bodies[row];
			// The building gone: down to the ground.
			if (rappel.building != ecs::Entity{} &&
				(!lookup.IsAlive(rappel.building) || lookup.Get<gp::InactiveBody>(rappel.building) != nullptr ||
					Dead(lookup.Get<gp::Health>(rappel.building), lookup.Get<gp::Dying>(rappel.building))))
			{
				rappel.building = {};
				rappel.destZ = ground.At(position.XY());
			}
			// scrubVelocity2D(0), scrubVelocityZ(rate): straight down, no faster than its rate.
			body.velocity.x = {};
			body.velocity.y = {};
			if (body.velocity.z < rappel.rate)
				body.velocity.z = rappel.rate;
			if (rappel.building == ecs::Entity{})
				rappel.destZ = layers.empty() ? ground.At(position.XY()) : gp::LayerHeight(decks, ground, position.XY(), layers[row].layer);
			if (position.z > rappel.destZ)
				continue;
			position.z = rappel.destZ;
			body.velocity = {};
			body.Set(gp::physics_flag::PhysicsDriven, false);
			out.push_back({entities[row], rappel.building});
			commands.Remove<Rappel>(entities[row]);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::RopeHangerSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.rope_hangers";
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::gameplay::CombatDropSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.combat_drops";
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::gameplay::RappelSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.rappels";
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
