export module engine.gameplay.rts.match.systems.victory_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.match.components.victory_role;
export import engine.gameplay.rts.match.resources.match_outcome;
export import engine.gameplay.common.health.components.inactive_body;
export import engine.gameplay.rts.containment.resources.evacuations;
export import engine.gameplay.rts.containment.components.transport;
export import engine.gameplay.rts.lifecycle.resources.kill_requests;
export import engine.gameplay.rts.teams.resources.team_roster;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.rts.construction.components.under_construction;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.common.health.systems.health_system;
export import engine.gameplay.common.lifetime.resources.expirations;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.rts.vision.resources.shroud_map;

// A match's end (VictoryConditions::update, VICTORY_NOBUILDINGS, at the end of each tick once its deaths are known):
//   A player is defeated while nothing of theirs that counts for victory stands (hasSinglePlayerBeenDefeated: no
//   structure that is not dead, dying or dying this tick; one dying this tick that leaves a counted hole still counts).
//   Once the players still standing are all allies of the first of them (multipleAlliancesExist: mutually allied), a
//   single alliance is left: the match is over on this tick, and the first player still standing and each of their
//   allies (defeated ones too) have won (markAllianceVictorious).
//   A player newly defeated is marked so, and killed (Player::killPlayer): their containers are emptied first
//   (evacuateTeam), then everything of theirs dies this tick, but what goes to the neutral side instead (tech
//   buildings). After the first tick, the fallen player sees the whole map for good
//   (PartitionManager::revealMapForPlayerPermanently: a looker on every cell) and the presentation hears who fell (the
//   defeat message and sound).
// Only in a match (skirmish, LAN): a campaign's or the shell map's scripts decide those.
export namespace engine::gameplay
{
struct VictorySystem
{
	using Query = ecs::Query<ecs::Read<Owner>, ecs::Optional<VictoryRole>, ecs::Optional<Dying>, ecs::Optional<InactiveBody>, ecs::Optional<UnderConstruction>>;
	using Lookup = ecs::Lookup<ecs::Read<VictoryRole>, ecs::Read<Dying>, ecs::Read<InactiveBody>, ecs::Read<Transport>, ecs::Read<TeamMember>, ecs::Read<UnderConstruction>>;
	using Resources = ecs::Resources<ecs::Write<MatchOutcome>, ecs::Write<Evacuations>, ecs::Write<KillRequests>, ecs::Read<Deaths>, ecs::Read<Expirations>,
		ecs::Read<Relationships>, ecs::Read<TeamRoster>, ecs::Write<ShroudMap>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		MatchOutcome &outcome = context.Write<MatchOutcome>();
		Evacuations &evacuations = context.Write<Evacuations>();
		outcome.fallen.clear();
		evacuations.containers.clear();
		if (!outcome.enabled || outcome.players.empty())
			return;
		KillRequests &kills = context.Write<KillRequests>();
		const auto lookup = context.Lookup<Lookup>();

		// Dead this tick, though not yet gone.
		std::vector<ecs::Entity> dead(kills.entities.begin(), kills.entities.end());
		context.Read<Deaths>().ForEach([&](const Death &death) { dead.push_back(death.entity); });
		context.Read<Expirations>().ForEach([&](const Expiration &expired) { dead.push_back(expired.entity); });
		std::sort(dead.begin(), dead.end(), [](ecs::Entity a, ecs::Entity b) { return a.index < b.index || (a.index == b.index && a.generation < b.generation); });
		const auto effectivelyDead = [&](ecs::Entity entity) {
			return lookup.Get<Dying>(entity) != nullptr || lookup.Get<InactiveBody>(entity) != nullptr ||
				std::binary_search(dead.begin(), dead.end(), entity, [](ecs::Entity a, ecs::Entity b) { return a.index < b.index || (a.index == b.index && a.generation < b.generation); });
		};

		// Who still stands: a pass over the chunks, whose component sets answer most of it (a chunk with no victory
		// role holds nothing that counts; the dying and the inactive are whole chunks). Order does not matter here.
		std::vector<std::uint8_t> standing;
		const auto stand = [&](std::uint32_t player) {
			if (player >= standing.size())
				standing.resize(static_cast<std::size_t>(player) + 1, 0);
			standing[player] = 1;
		};
		const auto deadThisTick = [&](ecs::Entity entity) {
			return std::binary_search(dead.begin(), dead.end(), entity, [](ecs::Entity a, ecs::Entity b) { return a.index < b.index || (a.index == b.index && a.generation < b.generation); });
		};
		query.ForEachChunk([&](auto chunk) {
			const auto roles = chunk.template Get<VictoryRole>();
			if (roles.empty())
				return;
			const auto owners = chunk.template Get<Owner>();
			const auto entities = chunk.Entities();
			const bool dying = !chunk.template Get<Dying>().empty();
			const bool inactive = !chunk.template Get<InactiveBody>().empty();
			const bool underConstruction = !chunk.template Get<UnderConstruction>().empty();
			for (std::size_t row = 0; row < roles.size(); ++row)
			{
				const VictoryRole &role = roles[row];
				if (!role.Has(victory_role::CountsForVictory) && !role.Has(victory_role::LeavesCountedHole))
					continue;
				const bool isDead = dying || inactive || deadThisTick(entities[row]);
				if (role.Has(victory_role::CountsForVictory) && !isDead)
					stand(owners[row].player);
				// Dying this tick, finished: what it leaves stands at the tick's end.
				else if (role.Has(victory_role::LeavesCountedHole) && !dying && isDead && !underConstruction)
					stand(owners[row].player);
			}
		});

		const auto beaten = [&](std::uint32_t player) { return player >= standing.size() || standing[player] == 0; };
		const Relationships &relationships = context.Read<Relationships>();
		const auto allies = [&](std::uint32_t a, std::uint32_t b) { return a != b && relationships.Allies(a, b) && relationships.Allies(b, a); };

		if (!outcome.singleAllianceRemaining)
		{
			std::optional<std::uint32_t> alive;
			bool several = false;
			for (const MatchStanding &player : outcome.players)
			{
				if (beaten(player.player))
					continue;
				if (alive && !allies(*alive, player.player))
				{
					several = true;
					break;
				}
				if (!alive)
					alive = player.player;
			}
			if (!several)
			{
				outcome.singleAllianceRemaining = true;
				outcome.endTick = context.Tick();
				if (alive)
					for (MatchStanding &player : outcome.players)
						if (player.player == *alive || allies(player.player, *alive))
							player.victorious = true;
			}
		}

		// Everything owned, by entity index: only walked when someone falls.
		struct Owned
		{
			ecs::Entity entity;
			std::uint32_t player;
		};
		std::vector<Owned> owned;
		bool ownedGathered = false;
		const auto gatherOwned = [&] {
			if (ownedGathered)
				return;
			ownedGathered = true;
			query.ForEachChunk([&](auto chunk) {
				const auto owners = chunk.template Get<Owner>();
				const auto entities = chunk.Entities();
				for (std::size_t row = 0; row < owners.size(); ++row)
					owned.push_back({entities[row], owners[row].player});
			});
			std::sort(owned.begin(), owned.end(), [](const Owned &a, const Owned &b) { return a.entity.index < b.entity.index; });
		};

		const TeamRoster &roster = context.Read<TeamRoster>();
		const std::optional<std::uint32_t> neutral = roster.FindTeam("team");
		auto &commands = context.Commands();
		for (MatchStanding &player : outcome.players)
		{
			if (player.defeated || !beaten(player.player))
				continue;
			player.defeated = true;
			gatherOwned();
			if (context.Tick() > 1)
			{
				context.Write<ShroudMap>().RevealAllPermanently(player.player);
				outcome.fallen.push_back(player.player);
			}
			for (const Owned &each : owned)
				if (each.player == player.player && lookup.Get<Transport>(each.entity) != nullptr && !effectivelyDead(each.entity))
					evacuations.containers.push_back(each.entity);
			for (const Owned &each : owned)
			{
				if (each.player != player.player || effectivelyDead(each.entity))
					continue;
				const VictoryRole *role = lookup.Get<VictoryRole>(each.entity);
				if (role != nullptr && role->Has(victory_role::NeutralOnDefeat))
				{
					if (!neutral)
						continue;
					if (lookup.Get<TeamMember>(each.entity) != nullptr)
						commands.Set<TeamMember>(each.entity, TeamMember{*neutral});
					commands.Set<Owner>(each.entity, Owner{roster.TeamAt(*neutral).owner});
					continue;
				}
				kills.entities.push_back(each.entity);
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::VictorySystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.victory";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	// The composition orders it before the tick's deaths are carried out.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
