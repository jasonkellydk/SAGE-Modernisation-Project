export module games.generalszh.gameplay.containment.systems.assault_transport_system;
import std;

export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import games.generalszh.gameplay.containment.components.assault_transport;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.status.components.ai_activity;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.rts.containment.resources.cargo_manifest;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.rts.combat.components.attack_move;

// AssaultTransportAIUpdate::update for every assault transport, chunk-parallel. Its orders go out as AssaultOrders
// (ApplyAssaultOrders gives them, CMD_FROM_AI, after the step):
// - dead: giveFinalOrders (the members outside take on its order as a player's), once;
// - members gone, dead or ordered from outside (their last command source not its AI) leave the list (the last moved
//   into the gap, which this update then passes over, as the original);
// - its riders not yet members join (up to 10; wounded ones noted; ones joining after its first update are new, and stay
//   in until a new order);
// - attacking with only new members: pointless, it idles;
// - with a designated enemy alive: a healthy (full health), not new member inside gets out; outside, a wounded one (under
//   MembersGetHealedAtLifeRatio of its health) not already on its way comes back in, a sound one standing still that is
//   not after the enemy goes for it;
// - otherwise an attack-moving transport that is no longer attack-moving attack-moves on, an attacking one calls its
//   members back in (retrieveMembers: those outside not already coming).
export namespace generalszh::gameplay
{
struct AssaultOrder
{
	enum class Kind : std::uint8_t
	{
		Exit,            // aiExit(transport)
		Enter,           // aiEnter(transport)
		Attack,          // aiAttackObject(target)
		TransportIdle,   // the transport: aiIdle
		TransportMove,   // the transport: aiAttackMoveToPosition(goal)
		FinalAttack,     // a member: aiAttackObject(target, CMD_FROM_PLAYER)
		FinalAttackMove, // a member: aiAttackMoveToPosition(goal, CMD_FROM_PLAYER)
	};
	ecs::Entity transport;
	ecs::Entity member;
	ecs::Entity target;
	Engine::Math::FixedVector2 goal;
	Kind kind{Kind::Exit};
	std::uint8_t reserved[7]{};
};

struct AssaultOrders : ecs::ChunkOutputs<AssaultOrder>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::AssaultOrders>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.assault_orders";
};
}

export namespace generalszh::gameplay
{
struct AssaultTransportSystem
{
	using Query = ecs::Query<ecs::Write<AssaultTransport>, ecs::Read<engine::gameplay::Health>, ecs::Optional<engine::gameplay::Dying>,
		ecs::Optional<engine::gameplay::AttackTarget>, ecs::Optional<engine::gameplay::AttackMove>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Health>, ecs::Read<engine::gameplay::Dying>, ecs::Read<engine::gameplay::AiActivity>,
		ecs::Read<engine::gameplay::Passenger>, ecs::Read<engine::gameplay::MoveOrder>, ecs::Read<engine::gameplay::AttackTarget>,
		ecs::Read<engine::gameplay::Boarding>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::CargoManifest>, ecs::Write<AssaultOrders>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<AssaultOrders>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		using Kind = AssaultOrder::Kind;
		const auto lookup = context.Lookup<Lookup>();
		const gp::CargoManifest &manifest = context.Read<gp::CargoManifest>();
		auto &out = context.Write<AssaultOrders>().Slot(context);
		auto assaults = chunk.Get<AssaultTransport>();
		const auto healths = chunk.Get<gp::Health>();
		const auto dyings = chunk.Get<gp::Dying>();
		const auto attacks = chunk.Get<gp::AttackTarget>();
		const auto attackMoves = chunk.Get<gp::AttackMove>();
		const auto entities = chunk.Entities();
		const auto deadOf = [&](ecs::Entity entity) {
			const gp::Health *health = lookup.Get<gp::Health>(entity);
			return lookup.Get<gp::Dying>(entity) != nullptr || (health != nullptr && health->current <= Engine::Math::Fixed{});
		};
		for (std::size_t row = 0; row < assaults.size(); ++row)
		{
			AssaultTransport &assault = assaults[row];
			if (assault.done != 0)
				continue;
			const ecs::Entity self = entities[row];
			const auto order = [&](Kind kind, ecs::Entity member, ecs::Entity target = {}, Engine::Math::FixedVector2 goal = {}) {
				out.push_back({self, member, target, goal, kind});
			};
			// giveFinalOrders.
			if (!dyings.empty() || healths[row].current <= Engine::Math::Fixed{})
			{
				const bool targetAlive = lookup.IsAlive(assault.designatedTarget);
				for (std::uint32_t index = 0; index < assault.count; ++index)
				{
					const ecs::Entity member = assault.members[index];
					if (!lookup.IsAlive(member))
						continue;
					if (assault.isAttackObject != 0 && targetAlive)
						order(Kind::FinalAttack, member, assault.designatedTarget);
					else if (assault.isAttackMove != 0)
						order(Kind::FinalAttackMove, member, {}, assault.attackMoveGoal);
				}
				assault.done = 1;
				continue;
			}
			// Members gone, dead or taken over.
			for (std::uint32_t index = 0; index < assault.count; ++index)
			{
				const ecs::Entity member = assault.members[index];
				const gp::AiActivity *activity = lookup.IsAlive(member) ? lookup.Get<gp::AiActivity>(member) : nullptr;
				if (lookup.IsAlive(member) && !deadOf(member) && (activity == nullptr || activity->commanded == 0))
					continue;
				const std::uint32_t last = assault.count - 1;
				if (last > index)
				{
					assault.members[index] = assault.members[last];
					assault.healing[index] = assault.healing[last];
					assault.newMember[index] = assault.newMember[last];
				}
				else
				{
					assault.members[index] = {};
					assault.healing[index] = 0;
					assault.newMember[index] = 0;
				}
				--assault.count;
			}
			const auto wounded = [&](ecs::Entity entity) {
				const gp::Health *health = lookup.Get<gp::Health>(entity);
				return health != nullptr && health->current < health->maximum * assault.healAtLifeRatio;
			};
			// Riders not yet members.
			for (const ecs::Entity passenger : manifest.Aboard(self))
			{
				if (std::find(assault.members.begin(), assault.members.begin() + assault.count, passenger) != assault.members.begin() + assault.count)
					continue;
				if (assault.count >= AssaultTransport::Capacity)
					continue;
				assault.members[assault.count] = passenger;
				assault.healing[assault.count] = wounded(passenger) ? 1 : 0;
				assault.newMember[assault.count] = assault.newOccupantsAreNewMembers;
				++assault.count;
			}
			assault.newOccupantsAreNewMembers = 1;
			// isAttackPointless: attacking with only new members.
			const bool attacking = !attacks.empty() && (attacks[row].target != ecs::Entity{} || attacks[row].atPosition != 0);
			if (attacking && std::all_of(assault.newMember.begin(), assault.newMember.begin() + assault.count, [](std::uint8_t fresh) { return fresh != 0; }))
			{
				order(Kind::TransportIdle, self);
				continue;
			}
			const bool targetAlive = lookup.IsAlive(assault.designatedTarget) && !deadOf(assault.designatedTarget);
			if (targetAlive)
			{
				for (std::uint32_t index = 0; index < assault.count; ++index)
				{
					const ecs::Entity member = assault.members[index];
					if (!lookup.IsAlive(member))
						continue;
					const bool contained = lookup.Get<gp::Passenger>(member) != nullptr;
					const gp::Health *health = lookup.Get<gp::Health>(member);
					const bool healthy = health != nullptr && health->current == health->maximum;
					if (contained && healthy && assault.newMember[index] == 0)
						order(Kind::Exit, member);
					if (contained)
						continue;
					if (wounded(member))
					{
						if (lookup.Get<gp::Boarding>(member) == nullptr)
							order(Kind::Enter, member);
						continue;
					}
					const gp::MoveOrder *move = lookup.Get<gp::MoveOrder>(member);
					const bool moving = move != nullptr && move->mode != gp::MoveMode::Idle;
					const gp::AttackTarget *after = lookup.Get<gp::AttackTarget>(member);
					if (!moving && (after == nullptr || after->target != assault.designatedTarget))
						order(Kind::Attack, member, assault.designatedTarget);
				}
			}
			else if (assault.isAttackMove != 0 && attackMoves.empty())
				order(Kind::TransportMove, self, {}, assault.attackMoveGoal);
			else if (assault.isAttackObject != 0)
				for (std::uint32_t index = 0; index < assault.count; ++index)
				{
					const ecs::Entity member = assault.members[index];
					if (lookup.IsAlive(member) && lookup.Get<gp::Passenger>(member) == nullptr && lookup.Get<gp::Boarding>(member) == nullptr)
						order(Kind::Enter, member);
				}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::AssaultTransportSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.assault_transport";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
