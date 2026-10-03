export module games.generalszh.presentation.objects.systems.scenery_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.appearance.components.appearance;
export import games.generalszh.presentation.objects.resources.scenery;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.resources.object_frame;
export import games.generalszh.presentation.objects.resources.tree_breeze;
export import games.generalszh.presentation.objects.components.object_presentation;
import games.generalszh.presentation.objects.resources.detail_settings;
import games.generalszh.presentation.objects.algorithms.tree_bending;
import games.generalszh.presentation.objects.algorithms.tree_breeze_sway;
import games.generalszh.content.objects.model_states;
import engine.gameplay.rts.vision.resources.shroud_map;

// The client's scenery (Scenery) knocked about and drawn:
//   SceneryContactSystem, once a tick (Object::setTriggerAreaFlagsForChangeInPosition -> W3DTreeBuffer::unitMoved):
//   infantry and vehicles not immobile whose whole-unit position changed this tick reach every filed tree within
//   their radius (the smaller of a box's two) plus TREE_RADIUS_APPROX, measured in 3D from the tree's base: a crusher
//   (level above 1) topples a tree that topples, away from itself (its topple FX where it stands); anything else leans
//   a tree that leans out of its way (MoveOutwardTime over 1 frame). Trees cleared for construction are passed by.
//   SceneryBendSystem, each frame (the tree buffer's drawTrees): falling trees turn and bounce (their bounce FX), trees
//   down sink away, leaning trees lean out and back.
//   SceneryDrawSystem, each frame: each standing tree and prop not cleared away into the frame's scenery slot at its
//   place, turn and scale; a tree as the tree buffer bends it (toppled or leaning, darkened by its lean) and sways it in
//   its sway type's breeze; a prop in the model its template shows at the map's time of day (getBestModelNameForWB).
export namespace generalszh::presentation
{
struct SceneryContactSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Exclude<engine::gameplay::OffMap>>;
	using SideTables = ecs::SideTables<ecs::Read<TickPose>>;
	using Resources = ecs::Resources<ecs::Read<LookCatalog>, ecs::Write<Scenery>, ecs::Write<FxRequests>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		Scenery &scenery = context.Write<Scenery>();
		if (scenery.cellItems.empty())
			return;
		const LookCatalog &catalog = context.Read<LookCatalog>();
		auto &fx = context.Write<FxRequests>().pending;
		const auto &poses = context.SideRead<SideTables, TickPose>();
		const auto tick = static_cast<std::uint32_t>(context.Tick());
		query.ForEachChunk([&](auto chunk) {
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < definitions.size(); ++row)
			{
				const DefinitionLooks *mover = catalog.Of(definitions[row].index);
				const TickPose *pose = mover != nullptr && mover->bendsTrees ? poses.Get(entities[row]) : nullptr;
				// It moved to a new whole-unit spot this tick.
				if (pose == nullptr || (static_cast<int>(pose->previous[0]) == static_cast<int>(pose->current[0]) &&
										   static_cast<int>(pose->previous[1]) == static_cast<int>(pose->current[1])))
					continue;
				const float radius = mover->treeReach + TreeRadiusApprox;
				const std::uint64_t pusher = (static_cast<std::uint64_t>(entities[row].index) << 32) | entities[row].generation;
				scenery.ForEachBendingNear(pose->current[0], pose->current[1], radius, [&](std::uint32_t tree) {
					if (scenery.removed[tree] != 0)
						return;
					const DefinitionLooks *looks = catalog.Of(scenery.definition[tree]);
					if (looks == nullptr)
						return;
					const TreeMotion &motion = looks->treeMotion;
					const std::array<float, 3> &at = scenery.at[tree];
					const std::array<float, 3> delta{at[0] - pose->current[0], at[1] - pose->current[1], at[2] - pose->current[2]};
					if (!(radius * radius > delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]))
						return;
					if (mover->topplesTrees && motion.doTopple)
					{
						if (ToppleTree(scenery.bends[tree], motion, {delta[0], delta[1]}) && !motion.toppleFX.empty())
							fx.push_back({motion.toppleFX, at});
					}
					else if (motion.framesToMoveOutward > 1.0f)
					{
						// getUnitDirectionVector2D: the way it faces.
						const float facing = static_cast<float>(pose->currentFacing) * 6.283185307179586f / 4294967296.0f;
						PushTreeAside(scenery.bends[tree], motion, {delta[0], delta[1]}, {std::cos(facing), std::sin(facing)}, pusher, tick);
					}
				});
			}
		});
	}
};

struct SceneryBendSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<LookCatalog>, ecs::Read<PresentationFrame>, ecs::Write<Scenery>, ecs::Write<FxRequests>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		Scenery &scenery = context.Write<Scenery>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const float frames = static_cast<float>(context.Read<PresentationFrame>().seconds) * 30.0f;
		auto &fx = context.Write<FxRequests>().pending;
		for (std::size_t item = 0; item < scenery.Size(); ++item)
		{
			TreeBend &bend = scenery.bends[item];
			if (scenery.kind[item] != Scenery::Tree || scenery.removed[item] != 0 ||
				(bend.state == tree_bend_state::Upright && bend.pushDelta == 0.0f) || bend.state == tree_bend_state::Gone)
				continue;
			const DefinitionLooks *looks = catalog.Of(scenery.definition[item]);
			if (looks == nullptr)
				continue;
			std::array<float, 3> bounce{};
			if (StepTreeBend(bend, looks->treeMotion, frames, bounce) && !looks->treeMotion.bounceFX.empty())
			{
				const std::array<float, 3> &at = scenery.at[item];
				fx.push_back({looks->treeMotion.bounceFX, {at[0] + bounce[0], at[1] + bounce[1], at[2] + bounce[2] - bend.sunk}});
			}
		}
	}
};

struct SceneryDrawSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<LookCatalog>, ecs::Read<Scenery>, ecs::Read<TreeBreeze>, ecs::Read<PresentationFrame>,
		ecs::Read<engine::gameplay::ShroudMap>, ecs::Read<DetailSettings>, ecs::Write<ObjectInstances>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		const Scenery &scenery = context.Read<Scenery>();
		ObjectInstances &instances = context.Write<ObjectInstances>();
		if (scenery.Size() == 0 || instances.SlotCount() < 2)
			return;
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const TreeBreeze &breeze = context.Read<TreeBreeze>();
		const DetailSettings detail = context.Find<DetailSettings>() != nullptr ? *context.Find<DetailSettings>() : DetailSettings{};
		engine::gameplay::Appearance appearance;
		if (catalog.night)
			appearance.Set(catalog.bits.night);
		if (catalog.snow)
			appearance.Set(catalog.bits.snow);
		auto &slot = instances.SlotAt(instances.SlotCount() - 2);
		// getPropShroudStatusForPlayer: a prop whose four cells about it are all shrouded to the viewer is not drawn.
		const std::uint32_t viewer = context.Read<PresentationFrame>().viewer;
		const engine::gameplay::ShroudMap &shroud = context.Read<engine::gameplay::ShroudMap>();
		const auto shrouded = [&](const Engine::Math::FixedVector2 &at) {
			if (viewer == PresentationFrame::NoViewer)
				return false;
			const Engine::Math::Fixed half = shroud.CellSize() / Engine::Math::Fixed::FromInt(2);
			const auto cell = shroud.CellOf(at.x - half, at.y - half);
			for (const auto [dx, dy] : {std::pair{0, 0}, std::pair{1, 0}, std::pair{1, 1}, std::pair{0, 1}})
				if (shroud.Status(viewer, cell[0] + dx, cell[1] + dy) != engine::gameplay::CellShroud::Shrouded)
					return false;
			return true;
		};
		for (std::size_t item = 0; item < scenery.Size(); ++item)
		{
			const TreeBend &bend = scenery.bends[item];
			if (scenery.removed[item] != 0 || bend.state == tree_bend_state::Gone)
				continue;
			const DefinitionLooks *looks = catalog.Of(scenery.definition[item]);
			if (looks == nullptr || looks->stateLooks.empty() || (scenery.kind[item] == Scenery::Prop && shrouded(scenery.place[item])))
				continue;
			const auto state = static_cast<std::size_t>(looks->states.Empty() ? 0 : content::SelectModelState(looks->states, appearance.flags));
			ObjectInstance instance;
			instance.look = looks->stateLooks[state < looks->stateLooks.size() ? state : 0];
			const std::array<float, 3> &at = scenery.at[item];
			const float scale = scenery.scale[item];
			const float c = std::cos(scenery.angle[item]) * scale, s = std::sin(scenery.angle[item]) * scale;
			instance.world = {c, -s, 0, at[0], s, c, 0, at[1], 0, 0, scale, at[2], 0, 0, 0, 1};
			if (scenery.kind[item] == Scenery::Tree && looks->bufferTree)
			{
				BentTree(instance.world, bend, looks->treeMotion);
				// Pushed aside, it darkens (W3DTreeBuffer: sway.y = 1 - DarkeningFactor x pushAside).
				instance.shade = 1.0f - looks->treeMotion.darkening * bend.pushAside;
				ShearTree(instance.world, breeze.current[scenery.swayType[item]], at[2]);
			}
			instance.night = appearance.Test(catalog.bits.night);
			const bool shadowKindOn = looks->shadowKind == 0 || (looks->shadowKind == 2u ? detail.useShadowVolumes : detail.useShadowDecals);
			// drawTrees: a falling or fallen tree casts no shadow.
			instance.castsShadow = looks->castsShadow && shadowKindOn && bend.state == tree_bend_state::Upright;
			// A SHADOW_DECAL's is its texture laid on the terrain, not a cast one.
			instance.shadowDecal = instance.castsShadow ? looks->shadowDecal : DefinitionLooks::NoShadowDecal;
			if (looks->shadowDecal != DefinitionLooks::NoShadowDecal)
				instance.castsShadow = false;
			instance.receivesDynamicLights = looks->receivesDynamicLights;
			instance.lightSphere = {at[0], at[1], at[2] + looks->constructionHeight * 0.5f, looks->lightRadius};
			slot.push_back(instance);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::SceneryContactSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.scenery_contact";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};

template<>
struct SystemTraits<generalszh::presentation::SceneryBendSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.scenery_bend";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};

template<>
struct SystemTraits<generalszh::presentation::SceneryDrawSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.scenery_draw";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
