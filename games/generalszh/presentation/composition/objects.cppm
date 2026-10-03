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
import games.generalszh.presentation.objects.components.wave_guide_effects;
import games.generalszh.presentation.objects.components.vehicle_motion;
import games.generalszh.presentation.objects.algorithms.vehicle_motion_setup;
import games.generalszh.presentation.objects.algorithms.look_setup;
import games.generalszh.presentation.objects.systems.rope_view_systems;
import games.generalszh.presentation.objects.systems.radius_decal_view_system;
import games.generalszh.presentation.objects.systems.tracer_systems;
import games.generalszh.presentation.objects.systems.snow_system;
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
export import games.generalszh.presentation.objects.resources.bridge_art;
export import games.generalszh.presentation.roads.resources.road_geometry;
import games.generalszh.presentation.objects.systems.bridge_look_system;

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
	bool snow{false};  // the weather is snowy and models follow it (ForceModelsToFollowWeather)
	engine::effects::ParticleWorld *particles{nullptr};
	EffectsContent *effects{nullptr};
	BonePoses bones;
	std::u16string addCash{u"$%d"};   // GUI:AddCash
	std::u16string loseCash{u"-$%d"}; // GUI:LoseCash
	const BridgeArt *bridgeArt{nullptr}; // the map-drawn bridges' models and textures (none: no bridges drawn)
	const RoadGeometry *roads{nullptr};  // the map's roads as the road buffer draws them (none: no roads)
};

inline void EmplaceObjectResources(ecs::World &world, session::SessionView &game, const ObjectSetup &setup)
{
	world.EmplaceResource<PresentationFrame>();
	KnowMotionLooks(world.EmplaceResource<MotionLooks>(), game);
	LookCatalog &catalog = world.EmplaceResource<LookCatalog>();
	catalog.playerColors.assign(setup.playerColors.begin(), setup.playerColors.end());
	catalog.night = setup.night;
	catalog.snow = setup.snow;
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
	world.EmplaceResource<SnowField>();
	world.EmplaceResource<Scenery>();
	world.EmplaceResource<BridgeArt>(setup.bridgeArt != nullptr ? *setup.bridgeArt : BridgeArt{});
	world.EmplaceResource<BridgeViews>();
	world.EmplaceResource<RoadGeometry>(setup.roads != nullptr ? *setup.roads : RoadGeometry{});
	world.EmplaceResource<WaveGuideCuesPlayed>();
}

// What goes with an object that goes: the particle systems riding it are destroyed as the original's (ParticleSystem::destroy,
// through destroyParticleSystemByID or a system losing its object): they emit no more and what is out lives on.
inline void BindObjectEffectReleases(ecs::World &world, engine::effects::ParticleWorld *particles)
{
	world.Side<DamageEmission>().OnRemove([particles](ecs::Entity, DamageEmission &emission) {
		for (const auto &attached : emission.systems)
			particles->Stop(attached.id);
	});
	// An object that goes stops its riding effects (~W3DModelDraw: stopClientParticleSystems; ParticleSystem::update: destroy()).
	world.Side<ConditionEmission>().OnRemove([particles](ecs::Entity, ConditionEmission &emission) {
		for (const auto &attached : emission.systems)
			particles->Stop(attached.id);
	});
	world.Side<FxEmission>().OnRemove([particles](ecs::Entity, FxEmission &riding) {
		for (const auto &attached : riding.systems)
			particles->Stop(attached.id);
	});
	world.Side<CrashTrailEmission>().OnRemove([particles](ecs::Entity, CrashTrailEmission &trail) {
		for (const auto &attached : trail.systems)
			particles->Stop(attached.id);
	});
	// A stealth grantor that goes takes its radius system (~GrantStealthBehavior: destroyParticleSystemByID).
	world.Side<GrantStealthView>().OnRemove([particles](ecs::Entity, GrantStealthView &view) {
		if (view.system != 0)
			particles->Stop(view.system);
	});
	// A laser special object that goes takes its flares (~LaserUpdate: destroyParticleSystemByID).
	world.Side<AbilityLaserView>().OnRemove([particles](ecs::Entity, AbilityLaserView &view) {
		for (const auto system : {view.muzzle, view.impact})
			if (system != 0)
				particles->Stop(system);
	});
	// A beacon that goes takes its smoke (its drawable gone, the system destroys itself).
	world.Side<BeaconLook>().OnRemove([particles](ecs::Entity, BeaconLook &look) {
		if (look.smoke != 0)
			particles->Stop(look.smoke);
	});
	// An uplink that goes takes its effects (killEverything).
	world.Side<UplinkEffects>().OnRemove([particles](ecs::Entity, UplinkEffects &effects) {
		for (const auto system : effects.systems)
			particles->Stop(system);
		for (const auto system : effects.orbitSystems)
			if (system != 0)
				particles->Stop(system);
	});
	// A flood wave that goes stops its riding systems; what is out lives on (ParticleSystem::update: destroy()).
	world.Side<WaveGuideEffects>().OnRemove([particles](ecs::Entity, WaveGuideEffects &effects) {
		for (const std::uint64_t system : effects.systems)
			if (particles != nullptr)
				particles->Stop(system);
	});
	// A vehicle that goes stops its emitters; what is out lives on.
	world.Side<MotionEmission>().OnRemove([particles](ecs::Entity, MotionEmission &emission) {
		for (std::uint32_t index = 0; index < emission.count && particles != nullptr; ++index)
			particles->Stop(emission.systems[index]);
	});
}

// The objects' own frame systems (stateless: one shared instance each): the models' library, where fire effects
// leave the drawn bones, the hanging and falling ropes, the decals of radii, spectre and grid, tracers, the clouds, the
// map-drawn bridges.
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
	static SnowSystem snow;
	registry.Register(snow);
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
	// The map-drawn bridges as the bridge buffer draws them, by their damage state.
	static BridgeLookSystem bridgeLooks;
	registry.Register(bridgeLooks);
}
}
