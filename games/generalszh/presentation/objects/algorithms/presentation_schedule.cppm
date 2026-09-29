export module games.generalszh.presentation.objects.algorithms.presentation_schedule;
import std;

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
export import games.generalszh.presentation.objects.systems.ability_presentation_systems;
export import games.generalszh.presentation.objects.systems.object_icon_systems;
export import games.generalszh.presentation.objects.systems.uplink_systems;
export import games.generalszh.presentation.objects.algorithms.vehicle_motion_setup;
export import games.generalszh.presentation.objects.algorithms.look_setup;

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
}

inline void RegisterPresentationTick(ecs::SystemRegistry &tick, MotionSampleSystem &motion, PoseSampleSystem &pose, DetectorPingSystem &pings,
	ExhaustSampleSystem &exhausts, HitFxSystem &hits, MissileIgnitionSystem &ignitions,
	HarvestPresentationSystem &harvest, CratePresentationSystem &crates, PromotionPresentationSystem &promotions, ChassisSystem &chassis,
	TrackLayingSystem &tracks, TreeContactSystem &treeContacts, CashPresentationSystem &cash, DisabledSoundSystem &disabledSounds, EmpSparkSystem &empSparks,
	AutoDepositPresentationSystem &autoDeposits, AbilityFeedbackSystem &abilities, UplinkStatusSystem &uplinks, AttachedParticleClearSystem &particleClears)
{
	// Things falling past their DestroyAttachedParticlesAtHeight lose their riding systems, after the tick's new ones.
	tick.Register(particleClears);
	tick.OrderBefore<AttachedParticleClearSystem, EmpSparkSystem>();
	tick.OrderBefore<AttachedParticleClearSystem, DetectorPingSystem>();
	// The uplinks follow the tick's status changes.
	tick.Register(uplinks);
	// Abilities' sounds with the tick's other one-shots; their flashes step with the frames.
	tick.Register(abilities);
	tick.OrderBefore<DisabledSoundSystem, AbilityFeedbackSystem>();
	// Payments float up after the floating texts aged, with the other cash.
	tick.Register(autoDeposits);
	tick.OrderBefore<HarvestPresentationSystem, AutoDepositPresentationSystem>();
	tick.OrderBefore<CashPresentationSystem, AutoDepositPresentationSystem>();
	tick.OrderBefore<ChassisSystem, AutoDepositPresentationSystem>();
	tick.Register(disabledSounds);
	tick.Register(empSparks);
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
	tick.OrderBefore<DetectorPingSystem, HarvestPresentationSystem>();
	tick.Register(motion);
	tick.Register(pose);
	// Units touch trees once their poses for the tick are in.
	tick.OrderBefore<PoseSampleSystem, TreeContactSystem>();
	tick.OrderBefore<CratePresentationSystem, TreeContactSystem>();
	tick.OrderBefore<MissileIgnitionSystem, TreeContactSystem>();
	tick.OrderBefore<HitFxSystem, TreeContactSystem>();
	tick.Register(pings);
	tick.Register(exhausts);
}

inline void RegisterPresentationFrame(ecs::SystemRegistry &frame, TreadRollSystem &treads, WheelRollSystem &wheels, MotionEmitterSystem &emitters, ObjectPresentationSystem &objects, EffectAttachmentSystem &attachments, DamageEffectSystem &damage, FireFxPlacementSystem &fire, FxPlaybackSystem &fx, ExhaustSystem &exhausts, LaserSystem &lasers, DynamicLightSystem &lights, TrackFadeSystem &trackFade,
	TreeBreezeSystem &treeBreeze, TreeBendSystem &treeBend, DebrisAnimationSystem &debris, MountedDrawSystem &mounted, TintStatusSystem &tints,
	ObjectIconSystem &icons, UplinkEffectSystem &uplinks, RiderTintSystem &riderTints)
{
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
}
}
