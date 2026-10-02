export module engine.gameplay.rts.movement.systems.airborne_target_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.airborne_target;
export import engine.gameplay.common.spatial.components.surface_layer;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.spatial.resources.deck_surfaces;
export import engine.gameplay.rts.movement.components.locomotion;

// AIUpdateInterface::doLocomotor, after its locomotor moved it: OBJECT_STATUS_AIRBORNE_TARGET set while it is higher
// over the ground (or the deck it is on: Object::getHeightAboveTerrain, TerrainLogic::getLayerHeight) than its current
// locomotor's AirborneTargetingHeight, cleared otherwise. Before physics steps the tick (the original's AI update comes
// before its PhysicsBehavior), so the spatial index classes it by where its movement left it. A thing without a
// locomotor (a parachute, flown by its own) keeps the threshold it was given. Chunk-parallel: each row writes only its
// own status.
export namespace engine::gameplay
{
inline bool IsAirborneTarget(Engine::Math::Fixed z, Engine::Math::Fixed under, Engine::Math::Fixed threshold) noexcept
{
	return z - under > threshold;
}

struct AirborneTargetSystem
{
	using Query = ecs::Query<ecs::Read<Transform>, ecs::Write<AirborneTarget>, ecs::Optional<Locomotion>, ecs::Optional<SurfaceLayer>>;
	using Resources = ecs::Resources<ecs::Read<GroundHeight>, ecs::Read<DeckSurfaces>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const GroundHeight &ground = context.Read<GroundHeight>();
		const DeckSurfaces &decks = context.Read<DeckSurfaces>();
		const auto transforms = chunk.Get<Transform>();
		auto statuses = chunk.Get<AirborneTarget>();
		const auto motions = chunk.Get<Locomotion>();
		const auto layers = chunk.Get<SurfaceLayer>();
		for (std::size_t row = 0; row < transforms.size(); ++row)
		{
			AirborneTarget &status = statuses[row];
			if (!motions.empty())
				status.height = motions[row].locomotor.airborneTargetingHeight;
			const auto &position = transforms[row].position;
			const std::uint8_t layer = layers.empty() ? GroundLayer : layers[row].layer;
			const Engine::Math::Fixed under = layer == GroundLayer ? ground.At(position.XY()) : LayerHeight(decks, ground, position.XY(), layer);
			status.airborne = IsAirborneTarget(position.z, under, status.height) ? 1u : 0u;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::AirborneTargetSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.airborne_target";
	// Its rows are independent: large chunks are shared out in pieces of 32 rows.
	static constexpr std::size_t PieceRows = 32;
	static constexpr SystemPhase Phase = SystemPhase::PreSimulation;
	// The composition orders it after what moves bodies before physics, and before physics.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
