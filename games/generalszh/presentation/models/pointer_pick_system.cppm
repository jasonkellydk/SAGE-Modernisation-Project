export module games.generalszh.presentation.models.pointer_pick_system;
import std;

export import games.generalszh.presentation.models.model_library_system;
export import games.generalszh.presentation.interaction.resources.interaction_resources;
export import games.generalszh.presentation.interaction.systems.interaction_systems;
export import games.generalszh.presentation.interaction.systems.build_placement_system;
import engine.ecs.system.system;
import engine.gameplay.common.spatial.components.transform;

// W3DView::pickDrawable's cast (getPickRay, RTS3DScene::castRay): once a frame, the ray from the camera through the
// pointer's pixel, twice the far clip long, against every drawn object's model as drawn (CastInstanceRay); every one
// it meets, nearest first. Which of them counts (its pick type, what the click wants) is the interaction's to decide:
// the first that qualifies is the nearest that qualifies, as castRay keeps it.
export namespace generalszh::presentation
{
struct PointerPickSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Transform>>;
	using Resources = ecs::Resources<ecs::Read<PointerInput>, ecs::Read<InteractionView>, ecs::Read<ObjectInstances>, ecs::Write<ModelLibrary>,
		ecs::Write<PointerHits>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		const PointerInput &pointer = context.Read<PointerInput>();
		const InteractionView &view = context.Read<InteractionView>();
		PointerHits &hits = context.Write<PointerHits>();
		hits.x = pointer.x;
		hits.y = pointer.y;
		hits.hits.clear();
		if (!view.valid)
			return;
		const auto direction = view.Ray(pointer.x, pointer.y);
		const float length = view.farClip * 2.0f;
		const Engine::Math::Vector3 start{view.eye[0], view.eye[1], view.eye[2]};
		const Engine::Math::Vector3 end{start.x + direction[0] * length, start.y + direction[1] * length, start.z + direction[2] * length};
		ModelLibrary &library = context.Write<ModelLibrary>();
		context.Read<ObjectInstances>().ForEach([&](const ObjectInstance &instance) {
			if (instance.key == 0 || instance.look >= library.entryOfLook.size())
				return;
			const std::uint32_t entry = library.entryOfLook[instance.look];
			if (entry >= library.entries.size() || library.loads[entry].status != ModelStatus::Ready)
				return;
			Graphics::ModelRayCaster ray{start, end};
			bool failed = false;
			if (CastInstanceRay(library.entries[entry], instance, ray, failed))
			{
				const float dx = ray.end.x - start.x, dy = ray.end.y - start.y, dz = ray.end.z - start.z;
				hits.hits.push_back({instance.key, std::sqrt(dx * dx + dy * dy + dz * dz) / length});
			}
		});
		std::ranges::stable_sort(hits.hits, {}, &PointerHit::fraction);
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::PointerPickSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.pointer_pick";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// Before the pointer's readers (they see this frame's pick).
	using Before = SystemTypeList<generalszh::presentation::PointerInteractionSystem, generalszh::presentation::BuildPlacementSystem>;
	using After = SystemTypeList<generalszh::presentation::ModelLibrarySystem>;
};
}
