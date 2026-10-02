export module games.generalszh.presentation.objects.algorithms.presentation_schedule;
import games.generalszh.presentation.objects.systems.scenery_systems;
import std;

export import games.generalszh.presentation.objects.systems.firestorm_presentation_systems;
export import games.generalszh.presentation.objects.systems.bone_fx_presentation_systems;
export import games.generalszh.presentation.objects.systems.projectile_stream_systems;
export import engine.ecs.core.world;
export import engine.ecs.system.system;
export import games.generalszh.presentation.objects.systems.vehicle_motion_systems;
export import games.generalszh.presentation.objects.systems.object_presentation_systems;
export import games.generalszh.presentation.objects.systems.effect_attachment_systems;
export import games.generalszh.presentation.objects.systems.detector_ping_systems;
export import games.generalszh.presentation.objects.systems.hit_fx_systems;
export import games.generalszh.presentation.objects.systems.harvest_presentation_systems;
export import games.generalszh.presentation.objects.systems.crate_presentation_systems;
export import games.generalszh.presentation.objects.systems.cash_presentation_systems;
export import games.generalszh.presentation.objects.systems.promotion_presentation_systems;
export import games.generalszh.presentation.objects.systems.dynamic_light_systems;
export import games.generalszh.presentation.objects.systems.track_mark_systems;
export import games.generalszh.presentation.objects.systems.tree_breeze_systems;
export import games.generalszh.presentation.objects.systems.tree_bend_systems;
export import games.generalszh.presentation.objects.systems.debris_systems;
export import games.generalszh.presentation.objects.systems.disable_presentation_systems;
export import games.generalszh.presentation.objects.systems.rotor_wash_system;
export import games.generalszh.presentation.objects.systems.repair_weld_system;
export import games.generalszh.presentation.objects.systems.jet_touchdown_sound_system;
export import games.generalszh.presentation.objects.systems.spectre_strafe_system;
export import games.generalszh.presentation.objects.systems.fade_presentation_system;
export import games.generalszh.presentation.objects.systems.ability_presentation_systems;
export import games.generalszh.presentation.objects.systems.object_icon_systems;
export import games.generalszh.presentation.objects.systems.uplink_systems;
export import games.generalszh.presentation.objects.algorithms.vehicle_motion_setup;
export import games.generalszh.presentation.objects.algorithms.look_setup;
export import games.generalszh.presentation.objects.systems.wave_guide_effect_system;

// Presentation's schedules, composed in one place for the host and the
// tests alike: its side-table components (registered with the simulation's
// world before it is finalized) and its stateless systems, once a tick and
// once a frame, in their order.
export namespace generalszh::presentation
{
void RegisterPresentationComponents(ecs::World &world)
{
	RegisterVehicleMotion(world);
	RegisterObjectPresentation(world);
	world.RegisterComponent<WaveGuideEffects>();
}

inline void RegisterPresentationTick(ecs::SystemRegistry &tick)
{
	// Its systems are stateless: one shared instance each.
	static MotionSampleSystem motion;
	static PoseSampleSystem pose;
	static DetectorPingSystem pings;
	static ExhaustSampleSystem exhausts;
	static HitFxSystem hits;
	static MissileIgnitionSystem ignitions;
	static BunkerCrashFxSystem bunkerCrashes;
	static HarvestPresentationSystem harvest;
	static CratePresentationSystem crates;
	static PromotionPresentationSystem promotions;
	static ChassisSystem chassis;
	static TrackLayingSystem tracks;
	static TreeContactSystem treeContacts;
	static SceneryContactSystem sceneryContacts;
	static CashPresentationSystem cash;
	static DisabledSoundSystem disabledSounds;
	static EmpSparkSystem empSparks;
	static RotorWashSystem rotorWash;
	static RepairWeldSystem welds;
	static JetTouchdownSoundSystem touchdowns;
	static SpectreStrafeSystem spectreStrafes;
	static FadePresentationSystem fades;
	static AutoDepositPresentationSystem autoDeposits;
	static AbilityFeedbackSystem abilities;
	static StealthGrantPresentationSystem stealthGrants;
	static UplinkStatusSystem uplinks;
	static AttachedParticleClearSystem particleClears;
	// Things falling past their DestroyAttachedParticlesAtHeight lose their riding systems, after the tick's new ones.
	tick.Register(particleClears);
	tick.OrderBefore<AttachedParticleClearSystem, EmpSparkSystem>();
	tick.OrderBefore<AttachedParticleClearSystem, DetectorPingSystem>();
	// The uplinks follow the tick's status changes.
	tick.Register(uplinks);
	// Abilities' sounds with the tick's other one-shots; their flashes step with the frames.
	tick.Register(abilities);
	tick.OrderBefore<DisabledSoundSystem, AbilityFeedbackSystem>();
	// Stealth grants flash after the abilities' flashes.
	tick.Register(stealthGrants);
	tick.OrderBefore<AbilityFeedbackSystem, StealthGrantPresentationSystem>();
	tick.OrderBefore<SpectreStrafeSystem, StealthGrantPresentationSystem>();
	// Payments float up after the floating texts aged, with the other cash.
	tick.Register(autoDeposits);
	tick.OrderBefore<HarvestPresentationSystem, AutoDepositPresentationSystem>();
	tick.OrderBefore<CashPresentationSystem, AutoDepositPresentationSystem>();
	tick.OrderBefore<ChassisSystem, AutoDepositPresentationSystem>();
	tick.Register(disabledSounds);
	tick.Register(empSparks);
	// Landing Chinooks wash the ground after the tick's sparks are out.
	tick.Register(rotorWash);
	tick.OrderBefore<EmpSparkSystem, RotorWashSystem>();
	// Repairing drones' weld sparks with the rotor wash.
	tick.Register(welds);
	tick.OrderBefore<RotorWashSystem, RepairWeldSystem>();
	tick.OrderBefore<RepairWeldSystem, SpectreStrafeSystem>();
	// Jets touching down screech with the welds.
	tick.Register(touchdowns);
	tick.OrderBefore<RepairWeldSystem, JetTouchdownSoundSystem>();
	tick.OrderBefore<JetTouchdownSoundSystem, SpectreStrafeSystem>();
	// After the fades, so after every sound system they follow.
	tick.OrderBefore<FadePresentationSystem, RepairWeldSystem>();
	// A Spectre's gattling smokes the ground where its fire walks, after the rotor wash.
	tick.Register(spectreStrafes);
	tick.OrderBefore<RotorWashSystem, SpectreStrafeSystem>();
	// Objects made fading (Rebel Ambush) start their fade with the tick, FadeSound with the tick's other sounds.
	tick.Register(fades);
	tick.OrderBefore<AbilityFeedbackSystem, FadePresentationSystem>();
	// After the EMP sparks, so after every host's sound and notice system (they run before the sparks).
	tick.OrderBefore<EmpSparkSystem, FadePresentationSystem>();
	tick.Register(tracks);
	tick.Register(treeContacts);
	tick.Register(harvest);
	tick.Register(crates);
	// Floating texts age before the tick's new ones float up.
	tick.OrderBefore<HarvestPresentationSystem, CratePresentationSystem>();
	tick.OrderBefore<CratePresentationSystem, HitFxSystem>();
	// Cash floats up after the crates' (both after the floating texts aged).
	tick.Register(cash);
	tick.OrderBefore<CratePresentationSystem, CashPresentationSystem>();
	// World animations past their time go before this tick's crates and promotions add theirs.
	tick.Register(promotions);
	tick.OrderBefore<PromotionPresentationSystem, CratePresentationSystem>();
	tick.OrderBefore<HarvestPresentationSystem, PromotionPresentationSystem>();
	tick.Register(chassis);
	tick.Register(hits);
	tick.Register(ignitions);
	tick.OrderBefore<HitFxSystem, MissileIgnitionSystem>();
	// Bunker busters crashing through after the tick's hits.
	tick.Register(bunkerCrashes);
	tick.OrderBefore<MissileIgnitionSystem, BunkerCrashFxSystem>();
	tick.OrderBefore<SceneryContactSystem, BunkerCrashFxSystem>();
	tick.OrderBefore<DetectorPingSystem, HarvestPresentationSystem>();
	tick.Register(motion);
	tick.Register(pose);
	// Units touch trees once their poses for the tick are in.
	tick.OrderBefore<PoseSampleSystem, TreeContactSystem>();
	tick.OrderBefore<CratePresentationSystem, TreeContactSystem>();
	tick.OrderBefore<MissileIgnitionSystem, TreeContactSystem>();
	tick.OrderBefore<HitFxSystem, TreeContactSystem>();
	// The client's own trees, likewise (after the object trees: both may play topple FX).
	tick.Register(sceneryContacts);
	tick.OrderBefore<TreeContactSystem, SceneryContactSystem>();
	tick.Register(pings);
	tick.Register(exhausts);
}

inline void RegisterPresentationFrame(ecs::SystemRegistry &frame)
{
	// Its systems are stateless: one shared instance each.
	static TreadRollSystem treads;
	static WheelRollSystem wheels;
	static MotionEmitterSystem emitters;
	static ObjectPresentationSystem objects;
	static EffectAttachmentSystem attachments;
	static DamageEffectSystem damage;
	static FireFxPlacementSystem fire;
	static FxPlaybackSystem fx;
	static ExhaustSystem exhausts;
	static LaserSystem lasers;
	static DynamicLightSystem lights;
	static TrackFadeSystem trackFade;
	static TreeBreezeSystem treeBreeze;
	static TreeBendSystem treeBend;
	static SceneryBendSystem sceneryBend;
	static SceneryDrawSystem sceneryDraw;
	static DebrisAnimationSystem debris;
	static MountedDrawSystem mounted;
	static TintStatusSystem tints;
	static ObjectIconSystem icons;
	static UplinkEffectSystem uplinks;
	static RiderTintSystem riderTints;
	// The uplinks' effects after the weapons' lasers began the frame's beams, their flares with the riding systems.
	frame.Register(uplinks);
	frame.OrderBefore<ObjectPresentationSystem, UplinkEffectSystem>();
	frame.OrderBefore<LaserSystem, UplinkEffectSystem>();
	frame.OrderBefore<UplinkEffectSystem, DamageEffectSystem>();
	// The icons over objects (drawIconUI) made or freed for this frame.
	frame.Register(icons);
	// Tints step before the objects are placed with them.
	frame.Register(tints);
	frame.OrderBefore<TintStatusSystem, ObjectPresentationSystem>();
	// Riders take their carriers' stepped tints before they are placed.
	frame.Register(riderTints);
	frame.OrderBefore<RiderTintSystem, ObjectPresentationSystem>();
	frame.Register(trackFade);
	frame.Register(treeBreeze);
	frame.Register(treeBend);
	frame.Register(debris);
	// Debris pieces step their animations before the objects are placed, their landing FX after the trees' and before FX play.
	frame.OrderBefore<DebrisAnimationSystem, ObjectPresentationSystem>();
	frame.OrderBefore<TreeBendSystem, DebrisAnimationSystem>();
	frame.OrderBefore<DebrisAnimationSystem, FxPlaybackSystem>();
	// Trees fall and lean before the objects are placed, their bounce FX before FX play.
	frame.OrderBefore<TreeBendSystem, ObjectPresentationSystem>();
	frame.OrderBefore<TreeBendSystem, FxPlaybackSystem>();
	// The map trees' breeze steps before the objects are placed (the tree buffer prepares its frame first).
	frame.OrderBefore<TreeBreezeSystem, ObjectPresentationSystem>();
	frame.Register(treads);
	frame.Register(wheels);
	frame.Register(emitters);
	// The rear tires turn by the last frame's powerslide (W3DTruckDraw sets it after turning them).
	frame.OrderBefore<WheelRollSystem, MotionEmitterSystem>();
	frame.Register(objects);
	// Mounted portable structures join the frame once their carriers are placed.
	frame.Register(mounted);
	// The client's scenery falls and leans with the object trees (its bounce FX before FX play), then joins the frame
	// once the objects have made its slots.
	frame.Register(sceneryBend);
	frame.OrderBefore<TreeBendSystem, SceneryBendSystem>();
	frame.OrderBefore<SceneryBendSystem, FxPlaybackSystem>();
	frame.OrderBefore<SceneryBendSystem, DebrisAnimationSystem>();
	frame.Register(sceneryDraw);
	frame.OrderBefore<SceneryBendSystem, SceneryDrawSystem>();
	frame.OrderBefore<TreeBreezeSystem, SceneryDrawSystem>();
	frame.OrderBefore<ObjectPresentationSystem, SceneryDrawSystem>();
	frame.OrderBefore<SceneryDrawSystem, MountedDrawSystem>();
	frame.Register(attachments);
	frame.Register(damage);
	frame.Register(fire);
	frame.Register(fx);
	frame.Register(exhausts);
	frame.Register(lasers);
	frame.Register(lights);
	// The frame's lights once its FX have lit their pulses and the objects are placed.
	frame.OrderBefore<FxPlaybackSystem, DynamicLightSystem>();
	frame.OrderBefore<ObjectPresentationSystem, DynamicLightSystem>();
	// Riding effects follow the objects as presented this frame, after the
	// vehicles' emitters; damage states then ask for their FX, which play last.
	frame.OrderBefore<ObjectPresentationSystem, EffectAttachmentSystem>();
	frame.OrderBefore<MotionEmitterSystem, EffectAttachmentSystem>();
	frame.OrderBefore<EffectAttachmentSystem, DamageEffectSystem>();
	frame.OrderBefore<DamageEffectSystem, FxPlaybackSystem>();
	// Weapons' fire FX onto their barrels as presented, before they play.
	frame.OrderBefore<ObjectPresentationSystem, FireFxPlacementSystem>();
	frame.OrderBefore<DamageEffectSystem, FireFxPlacementSystem>();
	frame.OrderBefore<FireFxPlacementSystem, FxPlaybackSystem>();
	// Exhausts trail the missiles as presented this frame, alongside the other riding systems.
	frame.OrderBefore<ObjectPresentationSystem, ExhaustSystem>();
	frame.OrderBefore<EffectAttachmentSystem, ExhaustSystem>();
	frame.OrderBefore<ExhaustSystem, DamageEffectSystem>();
	// Lasers ride on the objects as presented, their particle systems with the other riding ones.
	frame.OrderBefore<ObjectPresentationSystem, LaserSystem>();
	frame.OrderBefore<ExhaustSystem, LaserSystem>();
	frame.OrderBefore<LaserSystem, DamageEffectSystem>();
	// The flood waves' riding sprays and their tick's splashes, once the frame's FX played (the last to start systems).
	static WaveGuideEffectSystem waveGuides;
	frame.Register(waveGuides);
	frame.OrderBefore<FxPlaybackSystem, WaveGuideEffectSystem>();
}

// The weapons' projectile streams, drawn into the frame's beams after the lasers began them and the uplinks added theirs,
// through the projectiles as presented.
inline void RegisterProjectileStreams(ecs::SystemRegistry &frame)
{
	// Its systems are stateless: one shared instance each.
	static ProjectileStreamSystem streams;
	frame.Register(streams);
	frame.OrderBefore<ObjectPresentationSystem, ProjectileStreamSystem>();
	frame.OrderBefore<UplinkEffectSystem, ProjectileStreamSystem>();
	frame.OrderBefore<LaserSystem, ProjectileStreamSystem>();
}

// BoneFXUpdate's particle systems: started once a tick after the other tick systems cleared or started theirs, riding
// on their objects each frame once the objects are presented, with the other riding systems.
inline void RegisterBoneFx(ecs::SystemRegistry &tick, ecs::SystemRegistry &frame)
{
	// Its systems are stateless: one shared instance each.
	static BoneParticleSystem particles;
	static BoneFxRideSystem riding;
	tick.Register(particles);
	tick.OrderBefore<AttachedParticleClearSystem, BoneParticleSystem>();
	tick.OrderBefore<AutoDepositPresentationSystem, BoneParticleSystem>();
	tick.OrderBefore<ChassisSystem, BoneParticleSystem>();
	tick.OrderBefore<DetectorPingSystem, BoneParticleSystem>();
	frame.Register(riding);
	frame.OrderBefore<ObjectPresentationSystem, BoneFxRideSystem>();
	frame.OrderBefore<EffectAttachmentSystem, BoneFxRideSystem>();
	frame.OrderBefore<UplinkEffectSystem, BoneFxRideSystem>();
	frame.OrderBefore<LaserSystem, BoneFxRideSystem>();
	frame.OrderBefore<BoneFxRideSystem, DamageEffectSystem>();
}

// The firestorms' particle systems, started and sized before the other riding and FX systems touch the particle world.
inline void RegisterFirestorms(ecs::SystemRegistry &frame)
{
	// Its systems are stateless: one shared instance each.
	static FirestormPresentationSystem firestorms;
	frame.Register(firestorms);
	frame.OrderBefore<FirestormPresentationSystem, EffectAttachmentSystem>();
	frame.OrderBefore<FirestormPresentationSystem, ExhaustSystem>();
	frame.OrderBefore<FirestormPresentationSystem, LaserSystem>();
	frame.OrderBefore<FirestormPresentationSystem, UplinkEffectSystem>();
	frame.OrderBefore<FirestormPresentationSystem, MotionEmitterSystem>();
}
}
