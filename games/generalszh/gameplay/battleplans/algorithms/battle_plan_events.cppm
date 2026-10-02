export module games.generalszh.gameplay.battleplans.algorithms.battle_plan_events;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.battleplans.algorithms.battle_plan_bonuses;
export import games.generalszh.gameplay.battleplans.systems.battle_plan_system;
export import games.generalszh.gameplay.battleplans.resources.battle_plan_cues;
import games.generalszh.gameplay.orders.algorithms.unit_orders;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.health.algorithms.max_health;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.status.algorithms.disable_now;
import engine.gameplay.rts.stealth.components.stealth_detector;
import engine.gameplay.rts.lifecycle.resources.casualties;
import engine.ecs.query.query;

// BattlePlanUpdate's reach beyond its Strategy Center, once the systems have run:
// - ApplyBattlePlanEvents: the tick's BattlePlanEvents in order. Active: setBattlePlan(its plan); packing:
//   setBattlePlan(NONE); a bombardment waiting on its turret idles its AI; each told to the presentation.
// - SetBattlePlan (setBattlePlan): the plan its army had taken from its player (hold the line: its own maximum health
//   back down; search and destroy: its own sight back down and its stealth detector off), then the new one given
//   (bombardment; hold the line: its maximum health times StrategyCenterHoldTheLineMaxHealthScalar; search and destroy:
//   its sight times its scalar, its stealth detector on); none: every troop of its player's the plans reach paralyzed
//   for BattlePlanChangeParalyzeTime.
// - ApplyBattlePlanCasualties (onDelete): a center gone takes the plan it gave away from its player.
export namespace generalszh::gameplay
{
namespace battle_plan_detail
{
inline void Cue(GameWorld &game, ecs::Entity center, BattlePlanCue::Kind kind, PlanStatus plan)
{
	namespace gp = engine::gameplay;
	auto *cues = game.world.FindResource<BattlePlanCues>();
	const auto *ref = game.world.Get<gp::DefinitionRef>(center);
	const auto *owner = game.world.Get<gp::Owner>(center);
	const auto *at = game.world.Get<gp::Transform>(center);
	if (cues == nullptr || ref == nullptr || owner == nullptr || at == nullptr)
		return;
	cues->list.push_back({kind, plan, {}, ref->index, owner->player, center, at->position});
}

// paralyzeTroop over the player's objects.
inline void ParalyzeTroops(GameWorld &game, std::uint32_t player, const BattlePlanConfig &config)
{
	namespace gp = engine::gameplay;
	std::vector<ecs::Entity> troops;
	ecs::Query<ecs::Read<gp::Owner>, ecs::Read<gp::DefinitionRef>> owned(game.world);
	owned.ForEachChunk([&](auto chunk) {
		const auto owners = chunk.template Get<gp::Owner>();
		const auto refs = chunk.template Get<gp::DefinitionRef>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < owners.size(); ++row)
			if (owners[row].player == player && Qualifies(game.templates.DefinitionAt(refs[row].index).kinds, config.valid, config.invalid))
				troops.push_back(entities[row]);
	});
	for (const ecs::Entity troop : troops)
		gp::DisableNow(game.world, troop, gp::disabled_type::Paralyzed, game.tick + config.paralyzeTicks);
}

inline void SetDetector(GameWorld &game, ecs::Entity center, bool on)
{
	namespace gp = engine::gameplay;
	if (auto *detector = game.world.Get<gp::StealthDetector>(center))
	{
		detector->flags = on ? detector->flags | gp::stealth_detector_flag::Enabled : detector->flags & ~gp::stealth_detector_flag::Enabled;
		if (on)
			detector->nextScan = game.tick + 1; // setWakeFrame(UPDATE_SLEEP_NONE): it scans on the next update
	}
}
}

inline void SetBattlePlan(GameWorld &game, ecs::Entity center, BattlePlan &state, const BattlePlanConfig &config, PlanStatus plan)
{
	namespace gp = engine::gameplay;
	using namespace battle_plan_detail;
	const auto *owner = game.world.Get<gp::Owner>(center);
	if (owner == nullptr)
		return;
	const std::uint32_t player = owner->player;
	const Engine::Math::Fixed one = Engine::Math::Fixed::One();
	switch (state.affecting)
	{
	case PlanStatus::Bombardment: ChangeBattlePlan(game, player, PlanStatus::Bombardment, -1, BonusesOf(config, PlanStatus::Bombardment)); break;
	case PlanStatus::HoldTheLine:
		ChangeBattlePlan(game, player, PlanStatus::HoldTheLine, -1, BonusesOf(config, PlanStatus::HoldTheLine));
		if (config.centerMaxHealthScalar != one)
			if (auto *health = game.world.Get<gp::Health>(center))
				gp::SetMaxHealth(*health, health->maximum * (one / config.centerMaxHealthScalar), config.centerMaxHealthChange);
		break;
	case PlanStatus::SearchAndDestroy:
		ChangeBattlePlan(game, player, PlanStatus::SearchAndDestroy, -1, BonusesOf(config, PlanStatus::SearchAndDestroy));
		if (config.centerSightScalar != one)
			ScaleSight(game, center, one / config.centerSightScalar);
		if (config.centerDetectsStealth)
			SetDetector(game, center, false);
		break;
	case PlanStatus::None: break;
	}
	switch (plan)
	{
	case PlanStatus::None: ParalyzeTroops(game, player, config); break;
	case PlanStatus::Bombardment: ChangeBattlePlan(game, player, PlanStatus::Bombardment, 1, BonusesOf(config, PlanStatus::Bombardment)); break;
	case PlanStatus::HoldTheLine:
		// (Any scalar but 0, as the original tests it.)
		if (config.centerMaxHealthScalar != Engine::Math::Fixed{})
			if (auto *health = game.world.Get<gp::Health>(center))
				gp::SetMaxHealth(*health, health->maximum * config.centerMaxHealthScalar, config.centerMaxHealthChange);
		ChangeBattlePlan(game, player, PlanStatus::HoldTheLine, 1, BonusesOf(config, PlanStatus::HoldTheLine));
		break;
	case PlanStatus::SearchAndDestroy:
		if (config.centerSightScalar != one)
			ScaleSight(game, center, config.centerSightScalar);
		if (config.centerDetectsStealth)
			SetDetector(game, center, true);
		ChangeBattlePlan(game, player, PlanStatus::SearchAndDestroy, 1, BonusesOf(config, PlanStatus::SearchAndDestroy));
		break;
	}
	state.affecting = plan;
}

inline void ApplyBattlePlanEvents(GameWorld &game, std::span<const BattlePlanEvent> events)
{
	namespace gp = engine::gameplay;
	using Kind = BattlePlanEvent::Kind;
	for (const BattlePlanEvent &event : events)
	{
		auto *state = game.world.IsAlive(event.center) ? game.world.Get<BattlePlan>(event.center) : nullptr;
		const auto *ref = state != nullptr ? game.world.Get<gp::DefinitionRef>(event.center) : nullptr;
		const BattlePlanConfig *config = ref != nullptr ? game.templates.BattlePlanOf(ref->index) : nullptr;
		if (config == nullptr)
			continue;
		switch (event.kind)
		{
		case Kind::Unpack: battle_plan_detail::Cue(game, event.center, BattlePlanCue::Kind::Unpack, event.plan); break;
		case Kind::Active:
			SetBattlePlan(game, event.center, *state, *config, event.plan);
			battle_plan_detail::Cue(game, event.center, BattlePlanCue::Kind::Active, event.plan);
			break;
		case Kind::Pack:
			SetBattlePlan(game, event.center, *state, *config, PlanStatus::None);
			battle_plan_detail::Cue(game, event.center, BattlePlanCue::Kind::Pack, event.plan);
			break;
		case Kind::Idle: battle_plan_detail::Cue(game, event.center, BattlePlanCue::Kind::Idle, event.plan); break;
		case Kind::AiIdle: AiIdle(game, event.center); break;
		}
	}
}

// onDelete: once a Strategy Center is gone, each player's plans are what its standing centers give (the gone one's plan
// taken back with its own bonuses).
inline void ApplyBattlePlanCasualties(GameWorld &game, std::span<const engine::gameplay::Casualty> casualties)
{
	namespace gp = engine::gameplay;
	auto *players = game.world.FindResource<BattlePlanPlayers>();
	if (players == nullptr)
		return;
	std::vector<std::uint32_t> gone;
	for (const gp::Casualty &casualty : casualties)
		if (!game.world.IsAlive(casualty.entity) && game.templates.BattlePlanOf(casualty.definition) != nullptr)
			gone.push_back(casualty.definition);
	if (gone.empty())
		return;
	std::array<std::array<std::int32_t, 3>, BattlePlanPlayers::Players> standing{};
	ecs::Query<ecs::Read<BattlePlan>, ecs::Read<gp::Owner>> centers(game.world);
	centers.ForEachChunk([&](auto chunk) {
		const auto plans = chunk.template Get<BattlePlan>();
		const auto owners = chunk.template Get<gp::Owner>();
		for (std::size_t row = 0; row < plans.size(); ++row)
			if (plans[row].affecting != PlanStatus::None && owners[row].player < BattlePlanPlayers::Players)
				++standing[owners[row].player][static_cast<std::size_t>(plans[row].affecting) - 1];
	});
	const BattlePlanConfig &config = *game.templates.BattlePlanOf(gone.front());
	for (std::uint32_t player = 0; player < BattlePlanPlayers::Players; ++player)
		for (std::size_t plan = 0; plan < 3; ++plan)
			while (players->players[player].counts[plan] > standing[player][plan])
				ChangeBattlePlan(game, player, static_cast<PlanStatus>(plan + 1), -1, BonusesOf(config, static_cast<PlanStatus>(plan + 1)));
}
}
