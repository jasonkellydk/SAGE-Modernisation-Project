export module games.generalszh.presentation.objects.systems.waypoint_path_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.objects.resources.waypoint_paths;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.algorithms.rally_lines;
export import games.generalszh.presentation.interaction.resources.interaction_resources;
export import games.generalszh.presentation.interaction.resources.mouse_tooltip;
export import games.generalszh.presentation.interaction.components.selected;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.movement.components.move_goal;
export import engine.gameplay.rts.movement.components.move_path;
export import engine.gameplay.rts.movement.resources.path_points;
export import engine.gameplay.rts.production.components.rally_point;
export import engine.gameplay.rts.aircraft.components.airfield;
export import engine.gameplay.rts.combat.components.aggression;
import Engine.Core.Math.FixedPresentation;

// W3DWaypointBuffer::drawWaypoints (GeneralsMD/Code/GameEngineDevice/Source/W3DDevice/GameClient/W3dWaypointBuffer.cpp),
// each frame.
// While InGameUI is in waypoint mode (Alt held): for each selected thing not IGNORED_IN_GUI whose AI follows a path
// (AI_FOLLOW_PATH: friend_getWaypointGoalPathSize, its current goal path index in it; a way out of a factory is
// AI_FOLLOW_EXITPRODUCTION_PATH, never shown), a line from where it is through the path's points from the one it heads
// for on (at most 512 points after its own), and the SCMNode model at each of those points.
// Otherwise, for each selected thing the viewed player controls, in selection order: a listening outpost
// (REVEALS_ENEMY_PATHS) with an enemy under the mouse within its vision range shows that enemy's way (its waypoint path,
// or else the goal it moves to) as an orange line, and ends the walk (the one line it allows); a thing with an exit that
// has a natural rally point and a rally point set shows its rally line (AppendRallyLine).
export namespace generalszh::presentation
{
// One thing's path into the frame's lines and nodes (`points`: from the one it heads for on).
inline void AppendWaypointPath(WaypointPaths &paths, const std::array<float, 3> &from, std::span<const std::array<float, 3>> points,
	std::uint32_t nodeLook)
{
	std::array<float, 3> previous = from;
	float along = 0.0f;
	std::size_t shown = 1; // the line's points: its own first
	for (const std::array<float, 3> &point : points)
	{
		if (shown < WaypointPaths::MaxDisplayNodes + 1)
		{
			const float dx = point[0] - previous[0], dy = point[1] - previous[1], dz = point[2] - previous[2];
			const float length = std::sqrt(dx * dx + dy * dy + dz * dz);
			// Tiled along the line (TILED_TEXTURE_MAP): a texture's width of it per width of line, carried on from segment to segment.
			paths.segments.push_back({previous, point, WaypointPaths::Width, WaypointPaths::Color, WaypointPaths::Texture,
				length / WaypointPaths::Width, along / WaypointPaths::Width});
			along += length;
			previous = point;
			++shown;
		}
		AppendWaypointNode(paths, point, nodeLook);
	}
}

struct WaypointPathSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Transform>,
		ecs::Read<engine::gameplay::MoveOrder>, ecs::Read<engine::gameplay::MovePath>, ecs::Read<engine::gameplay::MoveGoal>,
		ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::TeamMember>, ecs::Read<engine::gameplay::RallyPoint>,
		ecs::Read<engine::gameplay::Airfield>, ecs::Read<engine::gameplay::Aggression>>;
	using SideTables = ecs::SideTables<ecs::Read<Selected>>;
	using Resources = ecs::Resources<ecs::Read<PointerInput>, ecs::Read<generalszh::gameplay::ObjectTemplates>, ecs::Read<LookCatalog>,
		ecs::Read<TerrainHeightHandle>, ecs::Read<engine::gameplay::PathPoints>, ecs::Read<PresentationFrame>, ecs::Read<MouseTooltip>,
		ecs::Read<engine::gameplay::Relationships>, ecs::Write<WaypointPaths>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		WaypointPaths &paths = context.Write<WaypointPaths>();
		paths.segments.clear();
		paths.nodes.clear();
		const auto lookup = context.Lookup<Lookup>();
		const auto &ground = context.Read<TerrainHeightHandle>().at;
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const std::uint32_t nodeLook = catalog.waypointNodeLook;
		const auto &store = context.Read<gp::PathPoints>();
		const auto onGround = [&](Engine::Math::FixedVector2 point) {
			const float x = Engine::Math::ToFloat(point.x), y = Engine::Math::ToFloat(point.y);
			return Point3{x, y, ground ? ground(x, y) : 0.0f};
		};
		const auto at = [](const gp::Transform &transform) {
			return Point3{Engine::Math::ToFloat(transform.position.x), Engine::Math::ToFloat(transform.position.y),
				Engine::Math::ToFloat(transform.position.z)};
		};
		// Following its path still (AI_FOLLOW_PATH, not a way out of a factory): its move the point the path set last;
		// the points from the one it heads for on.
		std::vector<Point3> points;
		const auto followedPath = [&](ecs::Entity entity) {
			points.clear();
			const auto *order = lookup.Get<gp::MoveOrder>(entity);
			const auto *path = lookup.Get<gp::MovePath>(entity);
			// (The move's own point as ordered: a claimed goal cell may have moved its destination.)
			const auto *goal = lookup.Get<gp::MoveGoal>(entity);
			const Engine::Math::FixedVector2 heading = goal != nullptr ? goal->ordered : order != nullptr ? order->destination : Engine::Math::FixedVector2{};
			if (order == nullptr || path == nullptr || path->kind != gp::MovePathKind::Follow || path->next == 0 || path->next > path->count ||
				order->mode == gp::MoveMode::Idle || !(heading == store.At(path->block, path->next - 1)))
				return false;
			for (std::uint32_t index = path->next - 1; index < path->count; ++index)
				points.push_back(onGround(store.At(path->block, index)));
			return true;
		};
		const auto selected = context.SideRead<SideTables, Selected>().Entities();
		if (context.Read<PointerInput>().alt)
		{
			const auto &templates = context.Read<generalszh::gameplay::ObjectTemplates>();
			for (const ecs::Entity entity : selected)
			{
				const auto *ref = lookup.IsAlive(entity) ? lookup.Get<gp::DefinitionRef>(entity) : nullptr;
				if (ref == nullptr || templates.DefinitionAt(ref->index).Is("IGNORED_IN_GUI"))
					continue;
				const auto *transform = lookup.Get<gp::Transform>(entity);
				if (transform != nullptr && followedPath(entity))
					AppendWaypointPath(paths, at(*transform), points, nodeLook);
			}
			return;
		}
		const std::uint32_t viewer = context.Read<PresentationFrame>().viewer;
		const auto &relationships = context.Read<gp::Relationships>();
		const auto teamOf = [&](ecs::Entity entity) {
			const auto *member = lookup.Get<gp::TeamMember>(entity);
			return member != nullptr ? member->team : gp::Relationships::NoTeam;
		};
		for (const ecs::Entity entity : selected)
		{
			const auto *ref = lookup.IsAlive(entity) ? lookup.Get<gp::DefinitionRef>(entity) : nullptr;
			const auto *owner = ref != nullptr ? lookup.Get<gp::Owner>(entity) : nullptr;
			const auto *transform = owner != nullptr ? lookup.Get<gp::Transform>(entity) : nullptr;
			if (transform == nullptr || owner->player != viewer)
				continue;
			const DefinitionLooks *looks = catalog.Of(ref->index);
			if (looks == nullptr)
				continue;
			if (looks->revealsEnemyPaths)
			{
				const ecs::Entity enemy = context.Read<MouseTooltip>().mousedOver;
				const auto *enemyOwner = lookup.IsAlive(enemy) ? lookup.Get<gp::Owner>(enemy) : nullptr;
				const auto *enemyAt = enemyOwner != nullptr ? lookup.Get<gp::Transform>(enemy) : nullptr;
				if (enemyAt != nullptr && relationships.Enemies(teamOf(enemy), enemyOwner->player, teamOf(entity), owner->player))
				{
					const Point3 from = at(*enemyAt), self = at(*transform);
					const float dx = self[0] - from[0], dy = self[1] - from[1], dz = self[2] - from[2];
					const auto *aggression = lookup.Get<gp::Aggression>(entity);
					const float vision = aggression != nullptr ? Engine::Math::ToFloat(aggression->vision) : 0.0f;
					if (std::sqrt(dx * dx + dy * dy + dz * dz) <= vision && lookup.Get<gp::MoveOrder>(enemy) != nullptr)
					{
						if (!followedPath(enemy))
						{
							// Else the goal it moves to (getGoalPosition): its move's, or the last one it was given.
							const auto *order = lookup.Get<gp::MoveOrder>(enemy);
							const auto *goal = lookup.Get<gp::MoveGoal>(enemy);
							const Engine::Math::FixedVector2 destination =
								order->mode != gp::MoveMode::Idle ? order->destination : goal != nullptr ? goal->ordered : Engine::Math::FixedVector2{};
							const Point3 point = onGround(destination);
							if (std::sqrt(point[0] * point[0] + point[1] * point[1] + point[2] * point[2]) > 1.0f)
								points.push_back(point);
						}
						AppendEnemyPath(paths, from, points, nodeLook);
					}
				}
				break; // this one listening outpost satisfies the single path-line limit
			}
			const auto *rally = lookup.Get<gp::RallyPoint>(entity);
			if (rally == nullptr)
				continue;
			const Point3 center = at(*transform);
			const Point3 rallyAt = onGround(rally->at);
			if (looks->rallyExit == DefinitionLooks::RallyExit::Production)
			{
				// getExitPosition / getNaturalRallyPoint(FALSE): its UnitCreatePoint and NaturalRallyPoint in its frame.
				const float facing = Engine::Math::ToRadiansFloat(transform->facing);
				const float c = std::cos(facing), s = std::sin(facing);
				const auto inFrame = [&](const std::array<float, 3> &local) {
					return Point3{center[0] + c * local[0] - s * local[1], center[1] + s * local[0] + c * local[1], center[2] + local[2]};
				};
				AppendRallyLine(paths, inFrame(looks->exitCreatePoint), inFrame(looks->exitRallyPoint), rallyAt, center, facing, looks->majorRadius,
					looks->minorRadius, nodeLook);
			}
			else if (looks->rallyExit == DefinitionLooks::RallyExit::Helipad)
			{
				// ParkingPlaceBehavior: its HeliPark01 bone both door and natural rally point (none: no line).
				const auto *field = lookup.Get<gp::Airfield>(entity);
				if (field == nullptr || field->hasHelipad == 0)
					continue;
				const Point3 pad{Engine::Math::ToFloat(field->helipad.x), Engine::Math::ToFloat(field->helipad.y), Engine::Math::ToFloat(field->helipad.z)};
				AppendRallyLine(paths, pad, pad, rallyAt, center, Engine::Math::ToRadiansFloat(transform->facing), looks->majorRadius, looks->minorRadius,
					nodeLook);
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::WaypointPathSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.waypoint_paths";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
