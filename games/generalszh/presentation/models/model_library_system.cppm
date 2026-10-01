export module games.generalszh.presentation.models.model_library_system;
import std;

export import games.generalszh.presentation.models.model_library;
export import games.generalszh.presentation.objects.resources.object_frame;
export import games.generalszh.presentation.objects.systems.object_presentation_systems;
export import games.generalszh.presentation.objects.systems.debris_systems;
export import games.generalszh.presentation.objects.systems.dynamic_light_systems;
export import games.generalszh.presentation.objects.resources.placement_ghosts;
export import games.generalszh.presentation.objects.resources.rally_point_markers;
export import games.generalszh.presentation.objects.resources.move_hints;
import engine.ecs.system.system;
import engine.ecs.core.world;
import engine.gameplay.common.spatial.components.transform;
import Assets.Runtime;
import Assets.Cache;

// Once a frame, after the frame's objects are known: every look drawn this frame asks for its model (the first time),
// the structure being placed among them (InGameUI::placeBuildAvailable makes its drawable: its model loads even when
// nothing of its kind stands yet),
// models that finished loading bind, and the looks' clips become known to the presentation (LookClips: how long each
// look's animation runs, for the animation clocks). A batch system: the library is one resource.
export namespace generalszh::presentation
{
struct ModelLibrarySystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Transform>>;
	using Resources = ecs::Resources<ecs::Read<ObjectInstances>, ecs::Read<PlacementGhosts>, ecs::Read<RallyPointMarkers>, ecs::Read<MoveHints>, ecs::Read<LookCatalog>, ecs::Read<MotionLooks>,
		ecs::Read<ModelAssets>, ecs::Write<ModelLibrary>, ecs::Write<LookClips>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		Assets::AssetCache *cache = context.Read<ModelAssets>().cache;
		if (cache == nullptr)
			return;
		ModelLibrary &library = context.Write<ModelLibrary>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const MotionLooks &motion = context.Read<MotionLooks>();
		context.Read<ObjectInstances>().ForEach([&](const ObjectInstance &instance) {
			ResolveLook(library, catalog, &motion, *cache, instance.look);
			// The look its animation runs in, for its clip (a copy drawn with other parts shares its animation).
			if (instance.clipLook != instance.look)
				ResolveLook(library, catalog, &motion, *cache, instance.clipLook);
		});
		for (const ObjectInstance &ghost : context.Read<PlacementGhosts>().instances)
			ResolveLook(library, catalog, &motion, *cache, ghost.look);
		for (const ObjectInstance &marker : context.Read<RallyPointMarkers>().instances)
			ResolveLook(library, catalog, &motion, *cache, marker.look);
		for (const ObjectInstance &hint : context.Read<MoveHints>().instances)
			ResolveLook(library, catalog, &motion, *cache, hint.look);
		PollModels(library, *cache);
		LookClips &clips = context.Write<LookClips>();
		clips.byLook.resize(catalog.looks.size());
		for (std::uint32_t look = 0; look < clips.byLook.size(); ++look)
			if (!clips.byLook[look].Known())
				if (const auto clip = ClipOfLook(library, look))
					clips.byLook[look] = {clip->first, clip->second};
	}
};
}

export namespace generalszh::presentation
{
// The world's model library, loading from the process's asset cache, and the effects' animated bone lookups answered
// from it (a look's bone `seconds` into its animation).
inline void BindModelLibrary(ecs::World &world)
{
	world.EmplaceResource<ModelLibrary>();
	world.EmplaceResource<ModelAssets>(ModelAssets{Assets::Try_Get_Asset_Cache()});
	if (auto *poses = world.FindResource<BonePoses>())
		poses->animated = [&world](std::uint32_t look, float seconds, float start, std::string_view bone) {
			BoneLookup lookup;
			if (const auto found = AnimatedBoneOf(world.Resource<ModelLibrary>(), look, seconds, start, bone))
			{
				lookup.ready = lookup.found = true;
				lookup.position = found->first;
				lookup.yaw = found->second;
			}
			return lookup;
		};
}

// The load screen: every look of these instances asked for and waited on (the original loads the map's models before
// the match starts).
inline void WaitForInstanceModels(ecs::World &world, std::span<const ObjectInstance> instances)
{
	auto *library = world.FindResource<ModelLibrary>();
	const auto *assets = world.FindResource<ModelAssets>();
	const auto *catalog = world.FindResource<LookCatalog>();
	if (library == nullptr || assets == nullptr || assets->cache == nullptr || catalog == nullptr)
		return;
	for (const ObjectInstance &instance : instances)
	{
		ResolveLook(*library, *catalog, world.FindResource<MotionLooks>(), *assets->cache, instance.look);
		if (instance.clipLook != instance.look)
			ResolveLook(*library, *catalog, world.FindResource<MotionLooks>(), *assets->cache, instance.clipLook);
	}
	WaitForModels(*library, *assets->cache);
}
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::ModelLibrarySystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.model_library";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	// After the frame's instances are complete, and after this frame's readers of the clips (they read the clips known
	// by the frame before, as they always have).
	using After = SystemTypeList<generalszh::presentation::MountedDrawSystem, generalszh::presentation::DebrisAnimationSystem,
		generalszh::presentation::DynamicLightSystem>;
};
}
