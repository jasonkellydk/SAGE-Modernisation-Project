export module games.generalszh.gameplay.waveguide.algorithms.wave_guides;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.waveguide.components.wave_guide;
export import games.generalszh.gameplay.waveguide.resources.wave_guide_events;
export import games.generalszh.gameplay.waveguide.algorithms.wave_shape;
import games.generalszh.content.water.wave_guide_content;
import games.generalszh.content.objects.object_status;
import games.generalszh.content.objects.model_conditions;
import games.generalszh.gameplay.bridges.algorithms.bridges;
import games.generalszh.gameplay.objects.algorithms.object_factory;
import games.generalszh.gameplay.lifecycle.algorithms.retire_now;
import engine.ecs.query.query;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.status.components.status_flags;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.common.status.components.disabled_until;
import engine.gameplay.common.appearance.components.appearance;
import engine.gameplay.rts.movement.components.move_order;
import engine.gameplay.rts.movement.systems.movement_system;
import engine.gameplay.rts.navigation.components.navigation;
import engine.gameplay.rts.navigation.resources.navigation_grid;
import engine.gameplay.rts.navigation.algorithms.decks;
import engine.gameplay.rts.navigation.algorithms.clearance;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.rts.veterancy.components.experience;
import engine.gameplay.rts.lifecycle.resources.casualties;

// The flood waves' reach beyond themselves, between ticks (the session's aftermath):
//   ApplyDamDie (DamDie::onDie, GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Die/DamDie.cpp): each of the tick's
//     deaths whose definition has a DamDie that applies (DieMuxData: its death type as it lies dying, veterancy, status)
//     clears DISABLED_DEFAULT from every flood wave on the map (every shipped WAVEGUIDE thing is one). It comes after
//     the death's creation lists (CreateObjectDie goes first, as the dam's modules are ordered): a wave the dam's
//     OCL_DamDie makes is not disabled yet, so it is not enabled either, and its own first update disables it for good,
//     as in the original; the map's placed wave (GLA01's WaveGuideGLA01) is the one that sets off.
//   ApplyWaveGuideEvents (WaveGuideUpdate::startMoving, doDamage, the destroyObject calls), in the order they happened:
//     Start: the wave put at its path's first waypoint on the ground, facing the next, following the path from there
//       (aiFollowWaypointPath, CMD_FROM_AI: its route asked for afresh; setPathExtraDistance's 100 past the end is not
//       modelled: the wave ends 100 short of it);
//     Destroy: removed (destroyObject: no death);
//     Wet: OBJECT_STATUS_WET, and MODELCONDITION_FLOODED (its shadows off, the presentation's);
//     BridgeHit (the original's "temp demo hack"): a WaterWaveBridge (none: nothing more happens) put where the bridge
//       object is, facing along the bridge found there (TerrainLogic::findBridgeAt: the first whose deck holds the point;
//       none: facing 0), on the neutral player's team; the wave's BridgeParticle there, turned by the bridge's angle
//       and BridgeParticleAngleFudge; and the bridge deleted (TerrainLogic::deleteBridge: its deck broken for the
//       pathfinding, changeBridgeState, and its object destroyed).
export namespace generalszh::gameplay
{
namespace wave_guide_detail
{
namespace gp = engine::gameplay;

inline void ClearDefaultDisable(GameWorld &game, ecs::Entity entity)
{
	if (auto *disabled = game.world.Get<gp::Disabled>(entity))
		disabled->mask &= ~gp::disabled_type::Default;
	if (auto *timers = game.world.Get<gp::DisabledUntil>(entity))
		timers->until[0] = 0;
}

inline void MakeWet(GameWorld &game, ecs::Entity entity)
{
	auto &world = game.world;
	if (!world.IsAlive(entity))
		return;
	static constexpr std::uint64_t wet = std::uint64_t{1} << content::ObjectStatusBit("WET");
	if (!world.Has<gp::StatusFlags>(entity))
		world.Add<gp::StatusFlags>(entity);
	world.Get<gp::StatusFlags>(entity)->bits |= wet;
	static constexpr std::uint32_t flooded = content::ModelConditionBit("FLOODED");
	if (auto *look = world.Get<gp::Appearance>(entity))
		look->Set(flooded);
}

inline void Start(GameWorld &game, const WaveGuideEvent &event)
{
	auto &world = game.world;
	if (!world.IsAlive(event.guide))
		return;
	if (auto *transform = world.Get<gp::Transform>(event.guide))
	{
		transform->position = event.at;
		transform->facing = event.facing;
	}
	if (auto *order = world.Get<gp::MoveOrder>(event.guide))
	{
		*order = gp::FollowPath(game.waypoints, event.waypoint);
		if (auto *route = world.Get<gp::Route>(event.guide))
			route->planned = false;
	}
}

inline void BreakBridge(GameWorld &game, const WaveGuideEvent &event)
{
	auto &world = game.world;
	ecs::Entity bridgeObject;
	const Bridge *found = nullptr;
	ecs::Query<ecs::Read<Bridge>> bridges(world);
	bridges.ForEachChunk([&](auto chunk) {
		const auto rows = chunk.template Get<Bridge>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < rows.size(); ++row)
			if (found == nullptr && PointOnBridge(rows[row], event.at.XY()))
			{
				found = &rows[row];
				bridgeObject = entities[row];
			}
	});
	const Engine::Math::TurnAngle angle = found != nullptr ? AngleOf(found->to.XY() - found->from.XY()) : Engine::Math::TurnAngle{};
	const std::uint8_t layer = found != nullptr ? found->layer : std::uint8_t{0};
	const ecs::Entity made = SpawnObject(game, "WaterWaveBridge", event.at.XY(), angle, bridge_detail::NeutralTeam(game), "");
	if (!world.IsAlive(made))
		return;
	if (auto *transform = world.Get<gp::Transform>(made))
	{
		transform->position = event.at;
		transform->facing = angle;
	}
	// createParticleSystem(BridgeParticle) turned about z by the bridge's angle and the fudge.
	const auto *ref = world.IsAlive(event.guide) ? world.Get<gp::DefinitionRef>(event.guide) : nullptr;
	if (ref != nullptr)
		if (const auto guide = content::ReadWaveGuide(game.templates.DefinitionAt(ref->index), game.step))
		{
			WaveGuideCue cue{WaveGuideCue::Kind::Bridge, event.guide, ref->index, event.at};
			cue.yaw = angle + guide->bridgeParticleAngleFudge;
			world.Resource<WaveGuideCues>().list.push_back(cue);
		}
	if (!world.IsAlive(bridgeObject))
		return;
	// deleteBridge: Pathfinder::changeBridgeState(layer, false), then the bridge's object destroyed.
	if (layer != 0)
	{
		auto &grid = world.Resource<gp::NavigationGrid>();
		gp::SetDeckDestroyed(grid, game.ground, layer, true);
		for (gp::ClearancePlane &plane : grid.Clearance())
			gp::BuildDeckClearance(grid, plane, layer);
	}
	RetireNow(game, {bridgeObject});
}
}

inline void ApplyDamDie(GameWorld &game, std::span<const engine::gameplay::Casualty> casualties)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	bool opened = false;
	for (const gp::Casualty &casualty : casualties)
	{
		if (opened || casualty.departure == gp::Departure::Removed)
			continue;
		const auto filter = content::ReadDamDie(game.templates.DefinitionAt(casualty.definition));
		if (!filter)
			continue;
		const gp::Dying *dying = world.IsAlive(casualty.entity) ? world.Get<gp::Dying>(casualty.entity) : nullptr;
		const gp::Experience *experience = world.IsAlive(casualty.entity) ? world.Get<gp::Experience>(casualty.entity) : nullptr;
		const gp::StatusFlags *status = world.IsAlive(casualty.entity) ? world.Get<gp::StatusFlags>(casualty.entity) : nullptr;
		// Gone at once (no body left to ask): its death type taken as any the filter allows.
		const bool applies = dying != nullptr
			? filter->Applies(dying->deathType, experience != nullptr ? experience->level : 0u, status != nullptr ? status->bits : 0u)
			: filter->deathTypes != 0;
		opened = applies;
	}
	if (!opened)
		return;
	std::vector<ecs::Entity> waves;
	ecs::Query<ecs::Read<WaveGuide>> query(world);
	query.ForEachChunk([&](auto chunk) {
		for (const ecs::Entity entity : chunk.Entities())
			waves.push_back(entity);
	});
	for (const ecs::Entity wave : waves)
		wave_guide_detail::ClearDefaultDisable(game, wave);
}

inline void ApplyWaveGuideEvents(GameWorld &game)
{
	auto &events = game.world.Resource<WaveGuideEvents>().list;
	if (events.empty())
		return;
	const std::vector<WaveGuideEvent> list = events;
	events.clear();
	for (const WaveGuideEvent &event : list)
		switch (event.kind)
		{
		case WaveGuideEvent::Kind::Start:
			wave_guide_detail::Start(game, event);
			break;
		case WaveGuideEvent::Kind::Destroy:
			if (game.world.IsAlive(event.guide))
				RetireNow(game, {event.guide});
			break;
		case WaveGuideEvent::Kind::Wet:
			wave_guide_detail::MakeWet(game, event.target);
			break;
		case WaveGuideEvent::Kind::BridgeHit:
			wave_guide_detail::BreakBridge(game, event);
			break;
		}
}
}
