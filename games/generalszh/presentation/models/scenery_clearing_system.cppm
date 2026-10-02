export module games.generalszh.presentation.models.scenery_clearing_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.identity.components.definition_ref;
export import games.generalszh.gameplay.world.resources.map_scenery;
export import games.generalszh.presentation.objects.resources.scenery;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.models.model_library;
export import games.generalszh.presentation.objects.resources.presentation_resources;
import engine.gameplay.common.spatial.algorithms.footprint;
import Assets.Cache;
import games.generalszh.presentation.objects.algorithms.tree_bending;

// The client's scenery cleared for each structure the logic placed (W3DTreeBuffer::removeTreesForConstruction,
// W3DPropBuffer::removePropsForConstruction, through removeTreesAndPropsForConstruction), each clearing once, in order:
// a tree goes when the structure's footprint meets a cylinder of radius 2 * TREE_RADIUS_APPROX at its base; a prop when
// it meets one of twice its model's bounding sphere's radius (geomCollidesWithGeom, from above). A prop whose model has
// not loaded yet holds the clearings back until it has.
export namespace generalszh::presentation
{
namespace scenery_clearing_detail
{
// The model's bounding sphere's radius: its meshes' spheres put together (SphereClass::Add_Sphere).
inline std::optional<float> BoundingRadius(ModelLibrary &library, const LookCatalog &catalog, const MotionLooks &motion, Assets::AssetCache *cache,
	std::uint32_t look)
{
	// A prop never drawn yet (shrouded) asks for its model here.
	if (cache != nullptr)
		ResolveLook(library, catalog, &motion, *cache, look);
	const ModelEntry *entry = library.ReadyEntry(look);
	if (entry == nullptr)
	{
		if (look < library.entryOfLook.size() && library.entryOfLook[look] == ModelLibrary::NoModel)
			return 0.0f;
		return std::nullopt;
	}
	bool first = true;
	std::array<float, 3> centre{};
	float radius = 0.0f;
	for (const std::array<float, 4> &sphere : entry->collisionSpheres)
	{
		if (first)
		{
			centre = {sphere[0], sphere[1], sphere[2]};
			radius = sphere[3];
			first = false;
			continue;
		}
		const std::array<float, 3> between{sphere[0] - centre[0], sphere[1] - centre[1], sphere[2] - centre[2]};
		const float distance = std::sqrt(between[0] * between[0] + between[1] * between[1] + between[2] * between[2]);
		if (distance + sphere[3] <= radius)
			continue; // inside already
		if (distance + radius <= sphere[3])
		{
			centre = {sphere[0], sphere[1], sphere[2]};
			radius = sphere[3];
			continue;
		}
		const float grown = (distance + radius + sphere[3]) * 0.5f;
		const float shift = (grown - radius) / distance;
		for (std::size_t axis = 0; axis < 3; ++axis)
			centre[axis] += between[axis] * shift;
		radius = grown;
	}
	return radius;
}
}

struct SceneryClearingSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<gameplay::SceneryClearings>, ecs::Read<LookCatalog>, ecs::Read<MotionLooks>, ecs::Read<ModelAssets>,
		ecs::Write<ModelLibrary>, ecs::Write<Scenery>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		Scenery &scenery = context.Write<Scenery>();
		const auto &clearings = context.Read<gameplay::SceneryClearings>().list;
		if (scenery.clearingsApplied >= clearings.size())
			return;
		const LookCatalog &catalog = context.Read<LookCatalog>();
		ModelLibrary &library = context.Write<ModelLibrary>();
		const MotionLooks &motion = context.Read<MotionLooks>();
		Assets::AssetCache *cache = context.Read<ModelAssets>().cache;
		// The props' radii first: none may be missing.
		std::vector<float> radii(scenery.Size(), 2.0f * TreeRadiusApprox);
		for (std::size_t item = 0; item < scenery.Size(); ++item)
		{
			if (scenery.kind[item] != Scenery::Prop || scenery.removed[item] != 0)
				continue;
			const DefinitionLooks *looks = catalog.Of(scenery.definition[item]);
			if (looks == nullptr || looks->stateLooks.empty())
				continue;
			const auto radius = scenery_clearing_detail::BoundingRadius(library, catalog, motion, cache, looks->stateLooks[0]);
			if (!radius)
				return;
			radii[item] = 2.0f * *radius;
		}
		namespace gp = engine::gameplay;
		for (; scenery.clearingsApplied < clearings.size(); ++scenery.clearingsApplied)
		{
			const gameplay::SceneryClearing &clearing = clearings[scenery.clearingsApplied];
			for (std::size_t item = 0; item < scenery.Size(); ++item)
			{
				if (scenery.removed[item] != 0)
					continue;
				const auto reach = Engine::Math::Fixed::FromRatio(static_cast<std::int64_t>(std::llround(radii[item] * 65536.0f)), 65536);
				if (gp::FootprintsOverlap(clearing.footprint, clearing.at, clearing.facing, {gp::FootprintShape::Circle, reach, reach}, scenery.place[item],
						Engine::Math::TurnAngle{}))
					scenery.removed[item] = 1;
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::SceneryClearingSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.scenery_clearing";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
