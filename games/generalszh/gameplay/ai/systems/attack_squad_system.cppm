export module games.generalszh.gameplay.ai.systems.attack_squad_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.gameplay.ai.components.attack_squad;
export import games.generalszh.gameplay.ai.resources.ai_players;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.status.components.status_flags;
export import engine.gameplay.common.status.components.script_status;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.random.resources.random_seed;
export import engine.gameplay.rts.combat.components.aggression;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.teams.resources.team_roster;
export import engine.gameplay.rts.combat.systems.targeting_system;
import games.generalszh.content.objects.object_status;
import Engine.Core.Math.FixedRandom;

// AIAttackSquadState for every unit attacking a team, chunk-parallel: while its victim is there (alive, in the spatial
// index) it keeps attacking it; else it chooses another from its squad (chooseVictim): a computer player's unit asleep
// chooses none, a passive one its last attacker; else by its player's difficulty (a player's order, and the script flag,
// as hard and normal): easy a random selectable member, normal the member nearest its centre (not dead, on the map as it
// is), hard the first selectable one. None left: its squad is done (SquadsDone: it idles once the step is over).
export namespace generalszh::gameplay
{
struct AttackSquadSystem
{
	using Query = ecs::Query<ecs::Write<AttackSquad>, ecs::Write<engine::gameplay::AttackTarget>, ecs::Read<engine::gameplay::Transform>,
		ecs::Read<engine::gameplay::Owner>, ecs::Optional<engine::gameplay::Aggression>, ecs::Optional<engine::gameplay::Health>,
		ecs::Optional<engine::gameplay::OffMap>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::StatusFlags>, ecs::Read<engine::gameplay::Health>,
		ecs::Read<engine::gameplay::Dying>, ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::OffMap>,
		ecs::Read<engine::gameplay::ScriptStatus>>;
	using Resources = ecs::Resources<ecs::Read<AttackSquads>, ecs::Read<AiPlayers>, ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::TeamRoster>,
		ecs::Read<engine::gameplay::SpatialIndex>, ecs::Read<engine::gameplay::RandomSeed>, ecs::Write<SquadsDone>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<SquadsDone>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const AttackSquads &squads = context.Read<AttackSquads>();
		const AiPlayers &ais = context.Read<AiPlayers>();
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const gp::TeamRoster &roster = context.Read<gp::TeamRoster>();
		const gp::SpatialIndex &spatial = context.Read<gp::SpatialIndex>();
		const std::uint64_t seed = context.Read<gp::RandomSeed>().value;
		const auto lookup = context.Lookup<Lookup>();
		auto &done = context.Write<SquadsDone>().Slot(context);
		auto orders = chunk.Get<AttackSquad>();
		auto attacks = chunk.Get<gp::AttackTarget>();
		const auto transforms = chunk.Get<gp::Transform>();
		const auto owners = chunk.Get<gp::Owner>();
		const auto aggressions = chunk.Get<gp::Aggression>();
		const auto healths = chunk.Get<gp::Health>();
		const auto offMap = chunk.Get<gp::OffMap>();
		const auto entities = chunk.Entities();
		const std::uint64_t tick = context.Tick();
		const std::uint64_t unselectable = std::uint64_t{1} << content::ObjectStatusBit("UNSELECTABLE");
		const auto dead = [&](ecs::Entity entity) {
			if (!lookup.IsAlive(entity) || lookup.Get<gp::Dying>(entity) != nullptr)
				return true;
			const gp::Health *health = lookup.Get<gp::Health>(entity);
			return health != nullptr && gp::IsDead(*health);
		};
		// Object::isSelectable: ALWAYS_SELECTABLE, or selectable (SELECTABLE, or what setSelectable made it), not UNSELECTABLE,
		// alive, not NO_SELECT.
		const auto selectable = [&](ecs::Entity entity) {
			const gp::DefinitionRef *ref = lookup.Get<gp::DefinitionRef>(entity);
			if (ref == nullptr)
				return false;
			const content::ObjectDefinition &definition = templates.DefinitionAt(ref->index);
			if (definition.Is("ALWAYS_SELECTABLE"))
				return true;
			const gp::StatusFlags *flags = lookup.Get<gp::StatusFlags>(entity);
			const gp::ScriptStatus *script = lookup.Get<gp::ScriptStatus>(entity);
			const bool set = script != nullptr && script->Has(gp::script_status::SelectableSet);
			const bool selectableNow = set ? script->Has(gp::script_status::SelectableValue) : definition.Is("SELECTABLE");
			return selectableNow && (flags == nullptr || (flags->bits & unselectable) == 0) && !dead(entity) && !definition.Is("NO_SELECT");
		};
		for (std::size_t row = 0; row < orders.size(); ++row)
		{
			gp::AttackTarget &attack = attacks[row];
			if (attack.target != ecs::Entity{} && spatial.Find(attack.target) != nullptr && !dead(attack.target))
				continue;
			const std::uint32_t player = owners[row].player;
			const bool human = player < roster.PlayerCount() && roster.PlayerAt(player).human;
			ecs::Entity victim;
			bool chosen = false;
			if (!human && !aggressions.empty())
			{
				if (aggressions[row].attitude == gp::attitude::Sleep)
					chosen = true;
				else if (aggressions[row].attitude == gp::attitude::Passive)
				{
					if (!healths.empty() && lookup.IsAlive(healths[row].lastAttacker))
						victim = healths[row].lastAttacker;
					chosen = true;
				}
			}
			if (!chosen)
			{
				const AiPlayer *ai = ais.Of(player);
				std::uint8_t difficulty = ai != nullptr ? ai->difficulty : 1; // a human's: the game's (normal)
				if (orders[row].fromPlayer != 0)
					difficulty = 2;
				if (squads.victimsAlwaysNormal)
					difficulty = 1;
				const auto members = squads.Members(orders[row].squad);
				if (difficulty == 0)
				{
					std::vector<ecs::Entity> live;
					for (const ecs::Entity member : members)
						if (selectable(member))
							live.push_back(member);
					if (!live.empty())
					{
						auto random = Engine::Math::Stream(seed, {tick, entities[row].index, entities[row].generation, 0x5A5Du});
						victim = live[static_cast<std::size_t>(Engine::Math::UniformInt(random, 0, static_cast<std::int64_t>(live.size()) - 1))];
					}
				}
				else if (difficulty == 1)
				{
					const Engine::Math::FixedVector2 at = transforms[row].position.XY();
					const bool mineOff = !offMap.empty();
					Engine::Math::Fixed best;
					for (const ecs::Entity member : members)
					{
						if (dead(member) || (lookup.Get<gp::OffMap>(member) != nullptr) != mineOff)
							continue;
						const gp::Transform *where = lookup.Get<gp::Transform>(member);
						if (where == nullptr)
							continue;
						const Engine::Math::Fixed distance = Engine::Math::DistanceSquared(where->position.XY(), at);
						if (victim == ecs::Entity{} || distance < best)
						{
							victim = member;
							best = distance;
						}
					}
				}
				else
					for (const ecs::Entity member : members)
						if (selectable(member))
						{
							victim = member;
							break;
						}
			}
			if (victim == ecs::Entity{})
			{
				attack = {};
				done.push_back(entities[row]);
				continue;
			}
			attack = gp::AttackTarget{victim, true};
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::AttackSquadSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.attack_squads";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<engine::gameplay::TargetingSystem>;
	using After = SystemTypeList<>;
};
}
