export module games.generalszh.presentation.objects.systems.tree_bend_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.components.object_presentation;
export import games.generalszh.presentation.objects.algorithms.tree_bending;
import Engine.Core.Math.FixedPresentation;

// Map trees knocked about by passing units (W3DTreeBuffer on the client):
//   TreeContactSystem, once a tick (Object::setTriggerAreaFlagsForChangeInPosition
//   -> W3DTreeBuffer::unitMoved): infantry and vehicles not immobile whose
//   whole-unit position changed this tick reach every tree that topples or
//   leans (DoTopple, or MoveOutwardTime over 2 frames) within their radius
//   plus 7 (3D, from the tree's base): a crusher (level above 1) topples a
//   tree that topples, away from itself (its topple FX); anything else leans
//   a tree that leans out of its way (MoveOutwardTime over 1 frame);
//   TreeBendSystem, each frame (prepareFrame): falling trees turn and bounce
//   (bounce FX), trees down sink away, leaning trees lean out and back.
export namespace generalszh::presentation
{
struct TreeContactSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Exclude<engine::gameplay::OffMap>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Write<TreeBend>, ecs::Read<TickPose>>;
	using Resources = ecs::Resources<ecs::Read<LookCatalog>, ecs::Read<engine::gameplay::SpatialIndex>, ecs::Write<FxRequests>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const engine::gameplay::SpatialIndex &index = context.Read<engine::gameplay::SpatialIndex>();
		auto &fx = context.Write<FxRequests>().pending;
		auto &bends = context.Side<SideTables, TreeBend>();
		const auto &poses = context.SideRead<SideTables, TickPose>();
		const auto lookup = context.Lookup<Lookup>();
		const auto tick = static_cast<std::uint32_t>(context.Tick());
		const Engine::Math::Fixed approx = Engine::Math::Fixed::FromInt(7);
		query.ForEachChunk([&](auto chunk) {
			const auto transforms = chunk.template Get<engine::gameplay::Transform>();
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < transforms.size(); ++row)
			{
				const DefinitionLooks *looks = catalog.Of(definitions[row].index);
				if (looks == nullptr || !looks->bufferTree)
					continue;
				const TreeMotion &motion = looks->treeMotion;
				// addTree: only trees that topple or lean out over more than 2 frames go in the area partition.
				if (!motion.doTopple && !(motion.framesToMoveOutward > 2.0f))
					continue;
				TreeBend *bend = bends.Get(entities[row]);
				if (bend == nullptr)
				{
					context.Commands().Add<TreeBend>(entities[row], TreeBend{});
					continue;
				}
				const std::array<float, 3> tree{Engine::Math::ToFloat(transforms[row].position.x), Engine::Math::ToFloat(transforms[row].position.y),
					Engine::Math::ToFloat(transforms[row].position.z)};
				index.ForEachWithin(transforms[row].position.XY(), approx, [&](const engine::gameplay::SpatialEntry &entry) {
					if (entry.entity == entities[row])
						return;
					const auto *moverDefinition = lookup.Get<engine::gameplay::DefinitionRef>(entry.entity);
					const DefinitionLooks *mover = moverDefinition != nullptr ? catalog.Of(moverDefinition->index) : nullptr;
					const TickPose *pose = poses.Get(entry.entity);
					if (mover == nullptr || !mover->bendsTrees || pose == nullptr)
						return;
					// It moved to a new whole-unit spot this tick.
					if (static_cast<int>(pose->previous[0]) == static_cast<int>(pose->current[0]) &&
						static_cast<int>(pose->previous[1]) == static_cast<int>(pose->current[1]))
						return;
					const float radius = mover->treeReach + TreeRadiusApprox;
					const std::array<float, 3> delta{tree[0] - pose->current[0], tree[1] - pose->current[1], tree[2] - pose->current[2]};
					if (!(radius * radius > delta[0] * delta[0] + delta[1] * delta[1] + delta[2] * delta[2]))
						return;
					if (mover->topplesTrees && motion.doTopple)
					{
						if (ToppleTree(*bend, motion, {delta[0], delta[1]}) && !motion.toppleFX.empty())
							fx.push_back({motion.toppleFX, tree});
					}
					else if (motion.framesToMoveOutward > 1.0f)
					{
						// getUnitDirectionVector2D: the way it faces.
						const float facing = static_cast<float>(pose->currentFacing) * 6.283185307179586f / 4294967296.0f;
						PushTreeAside(*bend, motion, {delta[0], delta[1]}, {std::cos(facing), std::sin(facing)},
							(static_cast<std::uint64_t>(entry.entity.index) << 32) | entry.entity.generation, tick);
					}
				});
			}
		});
	}
};

struct TreeBendSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Write<TreeBend>>;
	using Resources = ecs::Resources<ecs::Read<LookCatalog>, ecs::Read<PresentationFrame>, ecs::Write<FxRequests>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const float frames = static_cast<float>(context.Read<PresentationFrame>().seconds) * 30.0f;
		auto &fx = context.Write<FxRequests>().pending;
		auto &bends = context.Side<SideTables, TreeBend>();
		query.ForEachChunk([&](auto chunk) {
			const auto transforms = chunk.template Get<engine::gameplay::Transform>();
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < transforms.size(); ++row)
			{
				TreeBend *bend = bends.Get(entities[row]);
				if (bend == nullptr)
					continue;
				const DefinitionLooks *looks = catalog.Of(definitions[row].index);
				if (looks == nullptr)
					continue;
				std::array<float, 3> bounce{};
				if (StepTreeBend(*bend, looks->treeMotion, frames, bounce) && !looks->treeMotion.bounceFX.empty())
				{
					const auto &at = transforms[row].position;
					fx.push_back({looks->treeMotion.bounceFX,
						{Engine::Math::ToFloat(at.x) + bounce[0], Engine::Math::ToFloat(at.y) + bounce[1], Engine::Math::ToFloat(at.z) + bounce[2] - bend->sunk}});
				}
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::TreeContactSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.tree_contact";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};

template<>
struct SystemTraits<generalszh::presentation::TreeBendSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.tree_bend";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
