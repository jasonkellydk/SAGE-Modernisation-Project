export module games.generalszh.gameplay.ai.systems.mob_member_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.ai.components.mob_member;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.status.components.ai_activity;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.slaves.components.spawner;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.common.random.resources.random_seed;
import Engine.Core.Math.FixedRandom;

// MobMemberSlavedUpdate::update for every mob member, each tick (a batch: a member weighs its whole mob), its orders
// carried out after the tick (MobEvents):
//   its nexus gone (or dying): it dies too;
//   it looks only every 16th tick; then it remembers the nexus's victim (if any);
//   further than MustCatchUpRadius from the nexus (3D): the nexus moving, it heads for the nexus's goal (on its wander
//   set if it is nearer that goal than the nexus, else its panic set; the nexus's goal at the origin: the nexus itself;
//   not if already headed within 5 cells of it); the nexus standing, it rushes to it (panic); more than three times as
//   far, it counts a crisis look - past CatchUpCrisisBailTime of them it dies, past a third of that it rushes back;
//   near and moving: at random (one in eleven each) it goes on its wander, panic or normal set;
//   near and standing: the nexus idle (its player stopped it), it idles and forgets its victim; else, with none to
//   shoot at, it attacks the victim it remembers.
//   The nexus's goal and its own compared by straight distance (the original compares path lengths).
// Its own picking of targets (maySpawnSelfTaskAI -> getNextMoodTarget, for members the player ordered to attack),
// its personal tint and going prone as the nexus is hurt are not ported yet.
export namespace generalszh::gameplay
{
struct MobMemberSystem
{
	using Query = ecs::Query<ecs::Write<MobMember>, ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::MoveOrder>,
		ecs::Read<engine::gameplay::AttackTarget>, ecs::Read<engine::gameplay::DefinitionRef>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::MoveOrder>, ecs::Read<engine::gameplay::AttackTarget>,
		ecs::Read<engine::gameplay::AiActivity>, ecs::Read<engine::gameplay::Dying>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::RandomSeed>, ecs::Write<MobEvents>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using Engine::Math::Fixed;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const std::uint64_t seed = context.Read<gp::RandomSeed>().value;
		const auto lookup = context.Lookup<Lookup>();
		auto &events = context.Write<MobEvents>().list;
		events.clear();
		const std::uint64_t now = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			auto members = chunk.template Get<MobMember>();
			const auto transforms = chunk.template Get<gp::Transform>();
			const auto orders = chunk.template Get<gp::MoveOrder>();
			const auto attacks = chunk.template Get<gp::AttackTarget>();
			const auto definitions = chunk.template Get<gp::DefinitionRef>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < members.size(); ++row)
			{
				MobMember &member = members[row];
				const ecs::Entity me = entities[row];
				const MobMemberConfig *config = templates.MobMemberOf(definitions[row].index);
				if (config == nullptr)
					continue;
				const gp::Transform *nexus = lookup.IsAlive(member.nexus) && lookup.Get<gp::Dying>(member.nexus) == nullptr ? lookup.Get<gp::Transform>(member.nexus) : nullptr;
				if (nexus == nullptr)
				{
					events.push_back({me, MobOrder::Kill});
					member.nexus = {};
					continue;
				}
				if (++member.framesToWait < 16)
					continue;
				member.framesToWait = 0;
				const gp::MoveOrder *nexusOrder = lookup.Get<gp::MoveOrder>(member.nexus);
				const gp::AttackTarget *nexusAttack = lookup.Get<gp::AttackTarget>(member.nexus);
				if (nexusAttack != nullptr && nexusAttack->target != ecs::Entity{} && lookup.IsAlive(nexusAttack->target))
					member.primaryVictim = nexusAttack->target;
				const bool nexusMoving = nexusOrder != nullptr && nexusOrder->mode != gp::MoveMode::Idle;
				const auto &position = transforms[row].position;
				const Fixed apart = Engine::Math::LengthSquared(position - nexus->position);
				const Fixed catchUp = config->mustCatchUpRadius * config->mustCatchUpRadius;
				auto random = Engine::Math::Stream(seed, {now, me.index, me.generation, 0x30Bu});
				const auto move = [&](Engine::Math::FixedVector2 to) { events.push_back({me, MobOrder::Move, 0, {}, to}); };
				const auto useSet = [&](std::uint8_t set) { events.push_back({me, MobOrder::UseSet, set}); };
				if (apart > catchUp)
				{
					if (nexusMoving)
					{
						const Engine::Math::FixedVector2 goal = nexusOrder->destination;
						const Fixed nexusToGoal = Engine::Math::Distance(nexus->position.XY(), goal);
						const Fixed meToGoal = orders[row].mode != gp::MoveMode::Idle ? Engine::Math::Distance(position.XY(), orders[row].destination) : Fixed{};
						useSet(nexusToGoal > meToGoal ? 1u : 2u); // wander (slow down) or panic (catch up)
						if (Engine::Math::Length(goal) < Fixed::One())
							move(nexus->position.XY());
						else if (Engine::Math::Distance(orders[row].destination, goal) > Fixed::FromInt(5 * 10))
							move(goal);
					}
					else
					{
						useSet(2u);
						move(nexus->position.XY());
					}
					if (apart > catchUp * Fixed::FromInt(9))
					{
						++member.crisisTimer;
						if (member.crisisTimer > config->crisisBailTime)
						{
							events.push_back({me, MobOrder::Kill});
							continue;
						}
						if (member.crisisTimer > config->crisisBailTime / 3)
							move(nexus->position.XY());
					}
				}
				else if (orders[row].mode != gp::MoveMode::Idle)
				{
					member.crisisTimer = 0;
					const auto pick = Engine::Math::UniformInt(random, 0, 10);
					if (pick >= 1 && pick <= 3)
						useSet(pick == 1 ? 1u : pick == 2 ? 2u : 0u);
				}
				else
				{
					member.crisisTimer = 0;
					// AIUpdateInterface::isIdle: not moving, not attacking, not busy.
					const gp::AiActivity *activity = lookup.Get<gp::AiActivity>(member.nexus);
					const bool nexusIdle = !nexusMoving && (nexusAttack == nullptr || (nexusAttack->target == ecs::Entity{} && nexusAttack->atPosition == 0)) &&
						(activity == nullptr || !activity->Occupied());
					if (nexusIdle)
					{
						events.push_back({me, MobOrder::Idle});
						member.primaryVictim = {};
						continue;
					}
					const bool shooting = attacks[row].target != ecs::Entity{} && lookup.IsAlive(attacks[row].target);
					if (!shooting)
					{
						if (lookup.IsAlive(member.primaryVictim) && lookup.Get<gp::Dying>(member.primaryVictim) == nullptr)
							events.push_back({me, MobOrder::Attack, 0, member.primaryVictim});
						member.selfTasking = 0;
					}
				}
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::MobMemberSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.mob_member";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
