export module games.generalszh.gameplay.construction.algorithms.rebuild_holes;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.construction.components.rebuild_hole;
import games.generalszh.gameplay.construction.algorithms.building;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.object_id;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.health.algorithms.max_health;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.rts.match.resources.match_outcome;
import engine.ecs.query.query;

// GLA rebuild holes: RebuildHoleExposeDie (a finished structure of an active player's dies and leaves a hole: named as
// it was, its health HoleMaxHealth, its attackers turned on the hole) and RebuildHoleBehavior (after WorkerRespawnDelay a
// worker comes out of the hole and puts the structure up again free, resuming it if a worker is lost; the hole heals
// HoleHealthRegen% of its most a second; once the structure stands it takes the hole's name and the hole and its worker
// go; a hole's worker goes with it). Holes are looked at in the order they were made.
export namespace generalszh::gameplay
{
namespace rebuild_hole_detail
{
namespace gp = engine::gameplay;
using Engine::Math::Fixed;

inline const engine::config::Node *Module(const content::ObjectDefinition &object, std::string_view type)
{
	for (const content::ModuleEntry &module : object.modules)
		if (module.type == type && module.block != nullptr)
			return module.block;
	return nullptr;
}

inline Fixed Number(const engine::config::Node *block, std::string_view key, Fixed fallback)
{
	const auto *node = block != nullptr ? block->Find(key) : nullptr;
	if (node == nullptr || node->values.empty())
		return fallback;
	std::string_view text = node->Value();
	if (!text.empty() && text.back() == '%')
		text.remove_suffix(1); // parsePercentToReal
	return engine::config::values::ParseFixed(text).value_or(fallback);
}

// WorkerRespawnDelay (INI::parseDurationReal: milliseconds to ticks as a Real), kept in an UnsignedInt counter: truncated.
inline std::uint64_t WorkerDelay(const GameWorld &game, const content::ObjectDefinition &hole)
{
	const Fixed milliseconds = Number(Module(hole, "RebuildHoleBehavior"), "WorkerRespawnDelay", Fixed{});
	const Fixed ticks = milliseconds * Fixed::FromInt(static_cast<std::int64_t>(game.step.TicksPerSecond())) / Fixed::FromInt(1000);
	return ticks > Fixed{} ? static_cast<std::uint64_t>(ticks.Floor()) : 0u;
}

// AIUpdateInterface::transferAttack: everything attacking `from` attacks `to`.
inline void TransferAttack(GameWorld &game, ecs::Entity from, ecs::Entity to)
{
	ecs::Query<ecs::Write<gp::AttackTarget>> attackers(game.world);
	attackers.ForEachChunk([&](auto chunk) {
		auto targets = chunk.template Get<gp::AttackTarget>();
		for (auto &target : targets)
			if (target.target == from)
				target.target = to;
	});
}

// newWorkerRespawnProcess: its worker (if any) goes, and the next comes after the delay.
inline void NewWorkerRespawn(GameWorld &game, ecs::Entity hole, RebuildHole &state)
{
	if (game.world.IsAlive(state.worker))
		RetireNow(game, {state.worker});
	state.worker = {};
	const auto *ref = game.world.Get<gp::DefinitionRef>(hole);
	state.workerWait = ref != nullptr ? WorkerDelay(game, game.templates.DefinitionAt(ref->index)) : 0u;
}
}

// RebuildHoleExposeDie::onDie: a structure of a live player's (not the neutral one's) that was not still being built
// leaves its hole. `name` is the name it had; `definition` what it was.
inline ecs::Entity ExposeRebuildHole(GameWorld &game, ecs::Entity spawner, std::uint32_t definition, bool underConstruction, const std::string &holeName,
	Engine::Math::FixedVector3 position, Engine::Math::TurnAngle facing, std::uint32_t team, const std::optional<std::string> &name)
{
	using namespace rebuild_hole_detail;
	if (underConstruction || team >= game.roster.TeamCount() || definition == 0xFFFFFFFFu)
		return {};
	const std::uint32_t player = game.roster.TeamAt(team).owner;
	if (game.roster.PlayerAt(player).name.empty())
		return {}; // the neutral player
	// isPlayerActive: not a player already dead (one defeated by this very death is not dead yet as it dies: the
	// original's victory check comes after).
	if (const auto *outcome = game.world.FindResource<gp::MatchOutcome>())
		for (const auto &standing : outcome->players)
			if (standing.player == player && standing.defeated &&
				std::find(outcome->fallen.begin(), outcome->fallen.end(), player) == outcome->fallen.end())
				return {};
	const content::ObjectDefinition &dead = game.templates.DefinitionAt(definition);
	const engine::config::Node *expose = Module(dead, "RebuildHoleExposeDie");
	const ecs::Entity hole = SpawnObject(game, holeName, position.XY(), facing, team, "");
	if (!game.world.IsAlive(hole))
		return {};
	game.world.Get<gp::Transform>(hole)->position = position;
	if (name)
		game.names.Assign(*name, hole);
	if (auto *health = game.world.Get<gp::Health>(hole))
		gp::SetMaxHealth(*health, Number(expose, "HoleMaxHealth", Fixed{}), gp::MaxHealthChange::SameCurrent);
	RebuildHole state;
	state.rebuild = definition;
	state.spawner = spawner;
	NewWorkerRespawn(game, hole, state);
	game.world.Add<RebuildHole>(hole);
	*game.world.Get<RebuildHole>(hole) = state;
	const auto *transfer = expose != nullptr ? expose->Find("TransferAttackers") : nullptr;
	if (transfer == nullptr || engine::config::values::ParseBool(transfer->Value()).value_or(true))
		TransferAttack(game, spawner, hole);
	return hole;
}

// RebuildHoleBehavior::update for every hole (and the worker a hole no longer has).
inline void TendRebuildHoles(GameWorld &game)
{
	using namespace rebuild_hole_detail;
	// A worker whose hole died goes with it (RebuildHoleBehavior::onDie).
	std::vector<ecs::Entity> orphans;
	ecs::Query<ecs::Read<RebuildWorker>> workers(game.world);
	workers.ForEachChunk([&](auto chunk) {
		const auto rows = chunk.template Get<RebuildWorker>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < rows.size(); ++row)
			if (!game.world.IsAlive(rows[row].hole) || game.world.Get<gp::Dying>(rows[row].hole) != nullptr)
				orphans.push_back(entities[row]);
	});
	if (!orphans.empty())
		RetireNow(game, orphans);
	std::vector<std::pair<std::uint32_t, ecs::Entity>> holes;
	ecs::Query<ecs::Read<RebuildHole>> query(game.world);
	query.ForEachChunk([&](auto chunk) {
		const auto entities = chunk.Entities();
		for (const ecs::Entity entity : entities)
			if (game.world.Get<gp::Dying>(entity) == nullptr)
				if (const auto *id = game.world.Get<gp::ObjectId>(entity))
					holes.emplace_back(id->value, entity);
	});
	std::sort(holes.begin(), holes.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
	for (const auto &[id, hole] : holes)
	{
		if (!game.world.IsAlive(hole))
			continue;
		RebuildHole state = *game.world.Get<RebuildHole>(hole);
		ecs::Entity worker = game.world.IsAlive(state.worker) ? state.worker : ecs::Entity{};
		if (state.worker != ecs::Entity{} && worker == ecs::Entity{})
			NewWorkerRespawn(game, hole, state);
		ecs::Entity reconstructing = game.world.IsAlive(state.reconstructing) ? state.reconstructing : ecs::Entity{};
		if (state.reconstructing != ecs::Entity{} && reconstructing == ecs::Entity{})
		{
			NewWorkerRespawn(game, hole, state);
			worker = {};
			state.reconstructing = {};
		}
		if (worker == ecs::Entity{} && state.workerWait > 0 && --state.workerWait == 0)
		{
			const auto &holeKind = game.templates.DefinitionAt(game.world.Get<gp::DefinitionRef>(hole)->index);
			const auto *name = Module(holeKind, "RebuildHoleBehavior") != nullptr ? Module(holeKind, "RebuildHoleBehavior")->Find("WorkerObjectName") : nullptr;
			const auto &at = *game.world.Get<gp::Transform>(hole);
			const std::uint32_t team = game.world.Get<gp::TeamMember>(hole)->team;
			worker = name != nullptr ? SpawnObject(game, std::string(name->Value()), at.position.XY(), {}, team, "") : ecs::Entity{};
			if (game.world.IsAlive(worker))
			{
				state.worker = worker;
				game.world.Add<RebuildWorker>(worker);
				*game.world.Get<RebuildWorker>(worker) = {hole};
				if (reconstructing == ecs::Entity{})
					reconstructing = BeginConstruction(game, worker, game.templates.DefinitionAt(state.rebuild).name, at.position.XY(), at.facing, true);
				else
					OrderWork(game, worker, reconstructing); // aiResumeConstruction
				if (reconstructing != ecs::Entity{})
				{
					TransferAttack(game, hole, reconstructing);
					state.reconstructing = reconstructing;
				}
			}
		}
		// Heals HoleHealthRegen% of its most a second.
		if (auto *health = game.world.Get<gp::Health>(hole); health != nullptr && health->current < health->maximum)
		{
			const Fixed regen = Number(Module(game.templates.DefinitionAt(game.world.Get<gp::DefinitionRef>(hole)->index), "RebuildHoleBehavior"),
									   "HoleHealthRegen%PerSecond", Fixed::FromInt(10)) / Fixed::FromInt(100);
			gp::Heal(*health, regen / Fixed::FromInt(static_cast<std::int64_t>(game.step.TicksPerSecond())) * health->maximum, game.tick);
		}
		*game.world.Get<RebuildHole>(hole) = state;
		// Standing again: it takes the hole's name; the hole and its worker go.
		if (reconstructing != ecs::Entity{} && game.world.Get<gp::UnderConstruction>(reconstructing) == nullptr)
		{
			if (const auto name = game.names.NameOf(hole))
				game.names.Assign(*name, reconstructing);
			std::vector<ecs::Entity> gone{hole};
			if (game.world.IsAlive(worker))
				gone.push_back(worker);
			RetireNow(game, gone);
		}
	}
}

// The hole a structure left behind, if one stands (the AI's build list looks for it: RebuildHoleBehavior::getSpawnerID).
inline ecs::Entity HoleOf(const GameWorld &game, ecs::Entity spawner)
{
	ecs::Entity found;
	ecs::Query<ecs::Read<RebuildHole>> query(const_cast<ecs::World &>(game.world));
	query.ForEachChunk([&](auto chunk) {
		const auto rows = chunk.template Get<RebuildHole>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < rows.size(); ++row)
			if (rows[row].spawner == spawner)
				found = entities[row];
	});
	return found;
}
}
