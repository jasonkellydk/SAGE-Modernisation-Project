export module games.generalszh.gameplay.teams.algorithms.team_states;
import std;
import engine.gameplay.common.status.components.ai_activity;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.rts.construction.components.builder;
import engine.gameplay.rts.movement.components.locomotion;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.common.identity.components.definition_ref;

// Teams' own scripts (Team::updateState once a tick after the scripts, Team::updateGenericScripts in its player's update):
// a team just started runs its on-create script and counts itself for its on-destroyed script (which runs once when
// its living members fall to the threshold: its count less DestroyedThreshold of it, at least one fewer, not below
// none); a team idle two looks in a row with anyone alive runs its on-idle script; each generic script whose conditions
// hold for the team runs its actions (a one-shot one then no longer tried). A team with an enemy-sighted or all-clear
// script looks each tick whether any living member has an enemy within its vision range (centre to centre, alive, on or
// off the map as it is: where everything stands now), running the one or the other when that changes.
export namespace generalszh::gameplay
{
// How the teams reach the scripts (the session's runtime): a subroutine run for a team (runScript), a script's
// conditions looked at for it (evaluateConditions) and its actions run for it (friend_executeAction), whether a script
// is a one-shot, and whether a script of that name exists.
struct TeamScriptHooks
{
	std::function<void(const std::string &, std::uint32_t)> run;
	std::function<bool(const std::string &, std::uint32_t)> evaluate;
	std::function<void(const std::string &, std::uint32_t)> actions;
	std::function<bool(const std::string &)> oneShot;
	std::function<bool(const std::string &)> exists;
	// A team instance about to be deleted (Team's destructor: Player::preTeamDestroy for every player, so the computer
	// players drop it from their queues). Optional.
	std::function<void(std::uint32_t)> destroying;
};

// AIUpdateInterface::isIdle, as far as the port has states: not moving, attacking or building.
inline bool IsIdle(const GameWorld &game, ecs::Entity unit)
{
	namespace gp = engine::gameplay;
	if (const auto *order = game.world.Get<gp::MoveOrder>(unit); order != nullptr && order->mode != gp::MoveMode::Idle)
		return false;
	if (const auto *attack = game.world.Get<gp::AttackTarget>(unit); attack != nullptr && attack->target != ecs::Entity{})
		return false;
	// AI_BUSY is no idle state.
	if (const auto *busy = game.world.Get<gp::AiActivity>(unit); busy != nullptr && busy->busy != 0)
		return false;
	return game.world.Get<gp::Builder>(unit) == nullptr;
}

// Whether the unit has an AI at all (non-AI things count as idle and alive-less for the idle checks).
inline bool HasAi(const GameWorld &game, ecs::Entity unit)
{
	namespace gp = engine::gameplay;
	return game.world.Get<gp::MoveOrder>(unit) != nullptr || game.world.Get<gp::Locomotion>(unit) != nullptr;
}

inline std::string TeamScript(const GameWorld &game, std::uint32_t team, std::string_view key)
{
	return game.teams.At(game.roster.PrototypeOf(team)).Get<std::string>(key).value_or("");
}

// What a team's members may sight (ThePartitionManager: every living object, where it stands now).
struct Sighting
{
	ecs::Entity entity;
	Engine::Math::FixedVector2 at;
	std::uint32_t player{0};
	bool offMap{false};
};

// Squared lengths in 1/256ths of a unit, whole (map-scale distances squared fit).
inline std::int64_t SquaredRaw(Engine::Math::Fixed dx, Engine::Math::Fixed dy)
{
	const std::int64_t x = dx.Raw() >> 8, y = dy.Raw() >> 8;
	return x * x + y * y;
}

// Team::updateState for one team instance.
template<typename Sightings>
void UpdateTeamState(GameWorld &game, std::uint32_t index, const TeamScriptHooks &hooks, Sightings &&sightings)
{
	namespace gp = engine::gameplay;
	const auto alive = [&](ecs::Entity unit) { return game.world.IsAlive(unit) && game.world.Get<gp::Dying>(unit) == nullptr; };
	{
		gp::Team &team = game.roster.TeamAt(index);
		if (!team.alive || !team.active)
			return;
		const std::string onDestroyed = TeamScript(game, index, "teamOnDestroyedScript");
		if (team.justCreated)
		{
			team.justCreated = false;
			if (const std::string onCreate = TeamScript(game, index, "teamOnCreateScript"); !onCreate.empty())
				hooks.run(onCreate, index);
			if (!onDestroyed.empty())
			{
				gp::Team &again = game.roster.TeamAt(index);
				again.unitCount += static_cast<std::int32_t>(again.members.size());
				const auto threshold = game.teams.At(game.roster.PrototypeOf(index)).Get<Engine::Math::Fixed>("teamDestroyedThreshold");
				// m_curUnits - m_curUnits * threshold, as a float truncated to Int (below none clamps to none anyway).
				const Engine::Math::Fixed count = Engine::Math::Fixed::FromInt(again.unitCount);
				const Engine::Math::Fixed left = threshold ? count - count * *threshold : count;
				const std::int64_t whole = left < Engine::Math::Fixed{} ? 0 : left.Floor();
				again.destroyThreshold = static_cast<std::int32_t>(std::max<std::int64_t>(std::min<std::int64_t>(whole, again.unitCount - 1), 0));
			}
		}
		const std::string onSighted = TeamScript(game, index, "teamEnemySightedScript");
		const std::string onClear = TeamScript(game, index, "teamAllClearScript");
		if (!onSighted.empty() || !onClear.empty())
		{
			gp::Team &now = game.roster.TeamAt(index);
			now.prevSeeEnemy = now.seeEnemy;
			now.seeEnemy = false;
			bool anyone = false;
			const auto *relationships = game.world.FindResource<gp::Relationships>();
			const std::vector<Sighting> &others = sightings();
			for (const ecs::Entity member : now.members)
			{
				if (!alive(member))
					continue;
				anyone = true;
				const auto *where = game.world.Get<gp::Transform>(member);
				const auto *ref = game.world.Get<gp::DefinitionRef>(member);
				if (where == nullptr || ref == nullptr || relationships == nullptr)
					continue;
				const Engine::Math::Fixed range = game.templates.DefinitionAt(ref->index).visionRange;
				const bool offMap = game.world.Get<gp::OffMap>(member) != nullptr;
				const Engine::Math::FixedVector2 centre = where->position.XY();
				const std::int64_t reach = SquaredRaw(range, Engine::Math::Fixed{});
				const bool seen = std::any_of(others.begin(), others.end(), [&](const Sighting &other) {
					return other.entity != member && other.offMap == offMap && relationships->Enemies(now.owner, other.player) &&
						SquaredRaw(other.at.x - centre.x, other.at.y - centre.y) <= reach;
				});
				if (seen)
				{
					now.seeEnemy = true;
					break;
				}
			}
			if (anyone && now.prevSeeEnemy != now.seeEnemy)
			{
				const std::string &script = now.seeEnemy ? onSighted : onClear;
				if (!script.empty())
					hooks.run(script, index);
			}
		}
		if (!onDestroyed.empty())
		{
			gp::Team &now = game.roster.TeamAt(index);
			const std::int32_t before = now.unitCount;
			now.unitCount = static_cast<std::int32_t>(std::count_if(now.members.begin(), now.members.end(), alive));
			if (now.unitCount != before && now.unitCount <= now.destroyThreshold)
			{
				now.destroyThreshold = -1;
				hooks.run(onDestroyed, index);
			}
		}
		if (const std::string onIdle = TeamScript(game, index, "teamOnIdleScript"); !onIdle.empty())
		{
			gp::Team &now = game.roster.TeamAt(index);
			bool idle = true, anyone = false;
			for (const ecs::Entity member : now.members)
			{
				if (!alive(member) || !HasAi(game, member))
					continue;
				anyone = true;
				idle = idle && IsIdle(game, member);
			}
			const bool was = now.wasIdle;
			now.wasIdle = idle;
			if (anyone && idle && was)
				hooks.run(onIdle, index);
		}
	}
}

// ThePlayerList->updateTeamStates: each player's teams (TeamPrototype::updateState): each instance (newest first), then
// its instances left with nobody are deleted (not a singleton's, a player's default team, or one still being built).
// Members destroyed since leave their teams first (an Object leaves its team as it is destroyed).
inline void UpdateTeamStates(GameWorld &game, const TeamScriptHooks &hooks)
{
	namespace gp = engine::gameplay;
	for (std::uint32_t index = 0; index < game.roster.TeamCount(); ++index)
		std::erase_if(game.roster.TeamAt(index).members, [&](ecs::Entity member) { return !game.world.IsAlive(member); });
	// Gathered once, the first time a team looks for enemies.
	std::optional<std::vector<Sighting>> gathered;
	const auto sightings = [&]() -> const std::vector<Sighting> & {
		if (!gathered)
		{
			gathered.emplace();
			for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
				for (const ecs::Entity member : game.roster.TeamAt(team).members)
				{
					const auto *where = game.world.Get<gp::Transform>(member);
					if (where == nullptr || game.world.Get<gp::Dying>(member) != nullptr)
						continue;
					gathered->push_back({member, where->position.XY(), game.roster.TeamAt(team).owner, game.world.Get<gp::OffMap>(member) != nullptr});
				}
		}
		return *gathered;
	};
	// The instances' lists, one reused buffer (each list is still taken before it is walked, as a copy was).
	std::vector<std::uint32_t> instances;
	for (std::uint32_t player = 0; player < game.roster.PlayerCount(); ++player)
		for (std::uint32_t prototype = 0; prototype < game.roster.TeamCount(); ++prototype)
		{
			const gp::Team &level = game.roster.TeamAt(prototype);
			if (level.prototype != gp::Team::Own || !level.alive || level.owner != player)
				continue;
			game.roster.InstancesInto(prototype, instances);
			for (const std::uint32_t instance : instances)
				UpdateTeamState(game, instance, hooks, sightings);
			if (game.roster.TeamAt(prototype).singleton)
				continue;
			// The player's default team, looked up only when an empty active instance is met (the same answer: nothing
			// between here and there changes it).
			std::optional<std::optional<std::uint32_t>> defaultTeam;
			game.roster.InstancesInto(prototype, instances);
			for (const std::uint32_t instance : instances)
			{
				const gp::Team &team = game.roster.TeamAt(instance);
				if (!team.members.empty() || !team.active)
					continue;
				if (!defaultTeam)
					defaultTeam = game.roster.DefaultTeam(player);
				if (*defaultTeam && **defaultTeam == instance)
					continue;
				if (hooks.destroying)
					hooks.destroying(instance);
				game.roster.DeleteInstance(instance);
			}
		}
}

// Team::updateGenericScripts for the player's teams (Player::update: each of its level teams, each instance of it that
// exists, newest first).
inline void UpdateGenericScripts(GameWorld &game, std::uint32_t player, const TeamScriptHooks &hooks)
{
	std::vector<std::uint32_t> instances;
	for (std::uint32_t prototype = 0; prototype < game.roster.TeamCount(); ++prototype)
	{
		const auto &level = game.roster.TeamAt(prototype);
		if (level.prototype != engine::gameplay::Team::Own || level.owner != player)
			continue;
		game.roster.InstancesInto(prototype, instances);
		for (const std::uint32_t index : instances)
			for (int hook = 0; hook < 16; ++hook)
			{
				if ((game.roster.TeamAt(index).genericDone & (1u << hook)) != 0)
					continue;
				// The hook's key, spelled once (no string built per team and hook each tick).
				static constexpr std::array<std::string_view, 16> Keys{"teamGenericScriptHook0", "teamGenericScriptHook1", "teamGenericScriptHook2",
					"teamGenericScriptHook3", "teamGenericScriptHook4", "teamGenericScriptHook5", "teamGenericScriptHook6", "teamGenericScriptHook7",
					"teamGenericScriptHook8", "teamGenericScriptHook9", "teamGenericScriptHook10", "teamGenericScriptHook11", "teamGenericScriptHook12",
					"teamGenericScriptHook13", "teamGenericScriptHook14", "teamGenericScriptHook15"};
				const std::string script = TeamScript(game, index, Keys[static_cast<std::size_t>(hook)]);
				if (script.empty() || !hooks.exists(script))
				{
					game.roster.TeamAt(index).genericDone |= static_cast<std::uint16_t>(1u << hook);
					continue;
				}
				if (!hooks.evaluate(script, index))
					continue;
				if (hooks.oneShot(script))
					game.roster.TeamAt(index).genericDone |= static_cast<std::uint16_t>(1u << hook);
				hooks.actions(script, index);
			}
	}
}
}
