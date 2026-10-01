export module games.generalszh.presentation.composition.objects;
import std;

export import engine.ecs.core.world;
export import engine.ecs.system.system;
export import engine.effects.particles.simulation.particle_world;
export import games.generalszh.session.session_view;
export import games.generalszh.presentation.effects.effects_content;
export import games.generalszh.presentation.objects.resources.presentation_resources;
import games.generalszh.presentation.objects.resources.look_catalog;
import games.generalszh.presentation.objects.resources.object_frame;
import games.generalszh.presentation.objects.resources.floating_texts;
import games.generalszh.presentation.objects.resources.world_animations;
import games.generalszh.presentation.objects.resources.breeze;
import games.generalszh.presentation.objects.resources.cloud_layer;
import games.generalszh.presentation.objects.resources.tree_breeze;
import games.generalszh.presentation.objects.resources.dynamic_lights;
import games.generalszh.presentation.objects.resources.radius_cursor;
import games.generalszh.presentation.objects.components.effect_attachments;
import games.generalszh.presentation.objects.components.beacon_look;
import games.generalszh.presentation.objects.components.uplink_effects;
import games.generalszh.presentation.objects.components.vehicle_motion;
import games.generalszh.presentation.objects.algorithms.vehicle_motion_setup;
import games.generalszh.presentation.objects.algorithms.look_setup;
import games.generalszh.presentation.objects.systems.rope_view_systems;
import games.generalszh.presentation.objects.systems.radius_decal_view_system;
import games.generalszh.presentation.objects.systems.tracer_systems;
import games.generalszh.presentation.objects.systems.cloud_drift_system;
import games.generalszh.presentation.effects.scorch_marks;
import games.generalszh.presentation.effects.tracers;
import games.generalszh.presentation.camera.resources.camera_shakers;
import games.generalszh.presentation.camera.resources.view_filter;
import games.generalszh.presentation.models.model_library_system;
import games.generalszh.presentation.models.fire_fx_bone_system;
import games.generalszh.presentation.models.scenery_clearing_system;
import games.generalszh.presentation.objects.systems.scenery_systems;
import games.generalszh.presentation.objects.resources.scenery;

// The objects domain of the presentation: how the simulation's objects are drawn, animated and dressed in effects
// (the presentation's composition: which resources it keeps in the simulation's world, what goes with an object, and
// the systems that run each frame).
export namespace generalszh::presentation::composition
{
// What the objects' presentation is built from: the players' colours, the map's time of day, the effects' content and
// particle world, and where the renderer's models hold their bones.
struct ObjectSetup
{
	std::span<const std::array<float, 4>> playerColors;
	bool night{false}; // the map is at night and models follow its time of day (ForceModelsToFollowTimeOfDay)
	engine::effects::ParticleWorld *particles{nullptr};
	EffectsContent *effects{nullptr};
	BonePoses bones;
	std::u16string addCash{u"$%d"};   // GUI:AddCash
	std::u16string loseCash{u"-$%d"}; // GUI:LoseCash
};

inline void EmplaceObjectResources(ecs::World &world, session::SessionView &game, const ObjectSetup &setup)
{
	world.EmplaceResource<PresentationFrame>();
	KnowMotionLooks(world.EmplaceResource<MotionLooks>(), game);
	LookCatalog &catalog = world.EmplaceResource<LookCatalog>();
	catalog.playerColors.assign(setup.playerColors.begin(), setup.playerColors.end());
	catalog.night = setup.night;
	KnowLooks(catalog, game);
	world.EmplaceResource<LookClips>();
	world.EmplaceResource<ObjectInstances>();
	world.EmplaceResource<MountedInstances>();
	world.EmplaceResource<PresentedObjects>();
	world.EmplaceResource<ParticleWorldHandle>(ParticleWorldHandle{setup.particles, setup.effects});
	world.EmplaceResource<BonePoses>(setup.bones);
	BindModelLibrary(world);
	world.EmplaceResource<FxRequests>();
	world.EmplaceResource<FloatingTexts>();
	world.EmplaceResource<WorldAnimations>();
	world.EmplaceResource<Breeze>();
	world.EmplaceResource<CloudLayer>();
	world.EmplaceResource<TreeBreeze>();
	auto &texts = world.EmplaceResource<FloatingTextSettings>();
	texts.addCash = setup.addCash;
	texts.loseCash = setup.loseCash;
	world.EmplaceResource<WeaponExhausts>();
	world.EmplaceResource<WeaponRecoils>();
	world.EmplaceResource<WeaponFireLoops>();
	world.EmplaceResource<WeaponLasers>();
	world.EmplaceResource<WeaponStreams>();
	world.EmplaceResource<LaserRequests>();
	world.EmplaceResource<ActiveLasers>();
	world.EmplaceResource<LaserFrame>();
	world.EmplaceResource<SoundRequests>();
	world.EmplaceResource<ShakeRequests>();
	world.EmplaceResource<CameraShakers>();
	world.EmplaceResource<ViewFilter>();
	world.EmplaceResource<LightPulses>();
	world.EmplaceResource<DynamicLights>();
	world.EmplaceResource<ScorchMarks>();
	world.EmplaceResource<TrackSettings>();
	world.EmplaceResource<PresentationRandom>();
	world.EmplaceResource<EffectStats>();
	world.EmplaceResource<RadiusDecalViews>();
	world.EmplaceResource<RopeViews>();
	world.EmplaceResource<Tracers>();
	world.EmplaceResource<Scenery>();
}

// What goes with an object that goes: the particle systems riding it (stopped, so what is out lives on, or destroyed
// with it, as each effect's own drawable does).
inline void BindObjectEffectReleases(ecs::World &world, engine::effects::ParticleWorld *particles)
{
	world.Side<DamageEmission>().OnRemove([particles](ecs::Entity, DamageEmission &emission) {
		for (const auto &attached : emission.systems)
			particles->Stop(attached.id);
	});
	// An object that goes takes its riding effects at once.
	world.Side<ConditionEmission>().OnRemove([particles](ecs::Entity, ConditionEmission &emission) {
		for (const auto &attached : emission.systems)
			particles->Destroy(attached.id);
	});
	world.Side<FxEmission>().OnRemove([particles](ecs::Entity, FxEmission &riding) {
		for (const auto &attached : riding.systems)
			particles->Destroy(attached.id);
	});
	world.Side<CrashTrailEmission>().OnRemove([particles](ecs::Entity, CrashTrailEmission &trail) {
		for (const auto &attached : trail.systems)
			particles->Destroy(attached.id);
	});
	// A beacon that goes takes its smoke (its drawable gone, the system destroys itself).
	world.Side<BeaconLook>().OnRemove([particles](ecs::Entity, BeaconLook &look) {
		if (look.smoke != 0)
			particles->Destroy(look.smoke);
	});
	// An uplink that goes takes its effects (killEverything).
	world.Side<UplinkEffects>().OnRemove([particles](ecs::Entity, UplinkEffects &effects) {
		for (const auto system : effects.systems)
			particles->Destroy(system);
		for (const auto system : effects.orbitSystems)
			if (system != 0)
				particles->Destroy(system);
	});
	// A vehicle that goes stops its emitters; what is out lives on.
	world.Side<MotionEmission>().OnRemove([particles](ecs::Entity, MotionEmission &emission) {
		for (std::uint32_t index = 0; index < emission.count && particles != nullptr; ++index)
			particles->Stop(emission.systems[index]);
	});
}

// The objects' own frame systems (stateless: one shared instance each): the models' library, where fire effects
// leave the drawn bones, the hanging and falling ropes, the decals of radii, spectre and grid, tracers, the clouds.
inline void RegisterObjectFrameSystems(ecs::SystemRegistry &registry)
{
	static ModelLibrarySystem modelLibrary;
	static FireFxBoneSystem fireFxBones;
	static RadiusDecalViewSystem radiusDecalViews;
	static HangingRopeViewSystem hangingRopes;
	static FallingRopeViewSystem fallingRopes;
	static SpectreDecalViewSystem spectreDecalViews;
	static GridDecalViewSystem gridDecalViews;
	static TracerSystem tracers;
	static CloudDriftSystem cloudDrift;
	registry.Register(radiusDecalViews);
	registry.Register(hangingRopes);
	registry.Register(fallingRopes);
	registry.OrderBefore<HangingRopeViewSystem, FallingRopeViewSystem>();
	registry.Register(spectreDecalViews);
	registry.Register(gridDecalViews);
	registry.Register(tracers);
	static SceneryClearingSystem sceneryClearing;
	// The structures' clearings take the scenery under them before it is drawn; the drawn scenery's models then load.
	registry.Register(sceneryClearing);
	registry.OrderBefore<SceneryClearingSystem, SceneryDrawSystem>();
	registry.OrderBefore<SceneryBendSystem, SceneryClearingSystem>();
	registry.Register(modelLibrary);
	registry.OrderBefore<SceneryDrawSystem, ModelLibrarySystem>();
	registry.OrderBefore<SceneryClearingSystem, ModelLibrarySystem>();
	registry.Register(fireFxBones);
	registry.Register(cloudDrift);
}
}
