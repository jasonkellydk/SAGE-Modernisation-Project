export module games.generalszh.presentation.objects.systems.object_presentation_systems;
export import games.generalszh.presentation.objects.components.beacon_look;
import engine.gameplay.rts.match.resources.match_outcome;
import games.generalszh.presentation.objects.resources.detail_settings;
import engine.gameplay.rts.sciences.resources.player_sciences;
import engine.gameplay.rts.harvesting.components.resource_store;
import games.generalszh.gameplay.railroad.components.railcar;
import games.generalszh.presentation.objects.algorithms.draw_bones;
import games.generalszh.presentation.objects.systems.fade_presentation_system;
import games.generalszh.presentation.objects.algorithms.neutron_jitter;
export import engine.gameplay.rts.combat.components.neutron_flight;
export import engine.gameplay.common.appearance.components.draw_hidden;
import std;
export import engine.gameplay.rts.construction.components.sale;
import engine.gameplay.common.weapons.components.weapon_slots;
import engine.gameplay.common.appearance.components.indicator_color;
import engine.gameplay.common.appearance.components.occlusion_safe;
import engine.gameplay.common.weapons.resources.weapon_catalog;
import engine.gameplay.rts.parachute.algorithms.parachute_rigging;
export import games.generalszh.presentation.objects.algorithms.debris_animation;
import engine.gameplay.rts.stealth.components.stealth;
import engine.gameplay.rts.stealth.resources.detections;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.spatial.components.object_shroud;
import games.generalszh.presentation.objects.algorithms.tree_breeze_sway;
import games.generalszh.presentation.objects.algorithms.tree_bending;

export import engine.gameplay.common.status.components.disabled;
export import engine.ecs.system.system;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.attitude;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.appearance.components.appearance;
export import engine.gameplay.common.appearance.components.model_override;
export import engine.gameplay.common.appearance.components.draw_offset;
export import engine.gameplay.rts.containment.components.garrison;
export import engine.gameplay.rts.construction.components.construction_progress;
export import games.generalszh.presentation.objects.systems.chassis_systems;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.rts.combat.components.turret;
export import games.generalszh.presentation.objects.components.object_presentation;
export import games.generalszh.presentation.objects.components.vehicle_motion;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.resources.object_frame;
export import games.generalszh.presentation.objects.systems.vehicle_motion_systems;
export import games.generalszh.presentation.objects.algorithms.model_state_changes;
export import games.generalszh.presentation.objects.resources.breeze;
export import games.generalszh.presentation.objects.algorithms.barrel_placement;
export import engine.gameplay.rts.containment.components.mount;
export import engine.gameplay.rts.emp.components.emp_pulse;
export import engine.gameplay.common.appearance.components.part_overrides;
import games.generalszh.presentation.objects.algorithms.emp_look;
import Engine.Core.Math.FixedPresentation;
import engine.gameplay.rts.death.components.structure_topple;
import games.generalszh.content.objects.model_conditions;

// Presenting the simulation's visible objects, as presentation systems over
// its own entities (reading its components, writing side tables and the
// frame's outputs):
// - once a tick, the pose sample: where each was at the last two ticks;
// - each frame (in parallel per chunk): each drawn between those ticks, in
//   the look its conditions pick (its model state changing as W3DModelDraw
//   changes it: transitions, animations finishing first, idle animations
//   taking turns), in its player's colour, seen through when stealthed, with its
//   treads and tires as they have rolled; and the same objects for the
//   effects and sound to read.
export namespace generalszh::presentation
{
namespace object_presentation_detail
{
constexpr float RadiansPerUnit = 6.283185307179586f / 4294967296.0f;

inline std::uint64_t Key(ecs::Entity entity) noexcept { return (static_cast<std::uint64_t>(entity.index) << 32) | entity.generation; }

// A signed turn as radians in (-pi, pi].
// OffMap's reason for a portable structure mounted on its carrier (drawn with it: W3DDependencyModelDraw).
inline constexpr std::uint8_t MountedReason = 2;

inline float SignedRadians(Engine::Math::TurnAngle angle) noexcept { return static_cast<float>(static_cast<std::int32_t>(angle.units)) * RadiansPerUnit; }

// Between two angles (radians) the short way round.
inline float Between(float from, float to, float alpha) noexcept
{
	float delta = std::remainder(to - from, 6.283185307179586f);
	return from + delta * alpha;
}

inline std::array<float, 3> Position(const engine::gameplay::Transform &transform) noexcept
{
	return {Engine::Math::ToFloat(transform.position.x), Engine::Math::ToFloat(transform.position.y), Engine::Math::ToFloat(transform.position.z)};
}
}

struct PoseSampleSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Optional<engine::gameplay::Armament>,
		ecs::Optional<engine::gameplay::Turret>, ecs::Optional<engine::gameplay::AltTurret>, ecs::Optional<engine::gameplay::WeaponSlots>,
		ecs::Optional<engine::gameplay::OffMap>>;
	using SideTables = ecs::SideTables<ecs::Write<TickPose>, ecs::Write<WeaponPose>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::WeaponCatalog>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		using namespace object_presentation_detail;
		auto &poses = context.Side<SideTables, TickPose>();
		auto &weaponPoses = context.Side<SideTables, WeaponPose>();
		const auto transforms = chunk.Get<engine::gameplay::Transform>();
		const auto armaments = chunk.Get<engine::gameplay::Armament>();
		const auto turrets = chunk.Get<engine::gameplay::Turret>();
		const auto altTurrets = chunk.Get<engine::gameplay::AltTurret>();
		const auto weaponSets = chunk.Get<engine::gameplay::WeaponSlots>();
		const auto &weaponCatalog = context.Read<engine::gameplay::WeaponCatalog>();
		const std::uint64_t tick = context.Tick();
		const auto entities = chunk.Entities();
		const auto away = chunk.Get<engine::gameplay::OffMap>();
		for (std::size_t row = 0; row < transforms.size(); ++row)
		{
			// Off the map only a mounted portable structure is drawn (on its carrier), and a rider its container leaves in the
			// world (a fire base's, at its station: its drawable is not hidden).
			if (!away.empty() && away[row].reason != MountedReason && away[row].reason != engine::gameplay::off_map_reason::Stationed)
				continue;
			const auto at = Position(transforms[row]);
			const std::uint32_t facing = transforms[row].facing.units;
			if (TickPose *pose = poses.Get(entities[row]))
				*pose = {pose->current, at, pose->currentFacing, facing, 0};
			else
				context.Commands().Add<TickPose>(entities[row], TickPose{at, at, facing, facing, 0});
			if (armaments.empty())
				continue;
			const std::uint64_t fired = armaments[row].firedTick;
			const float turn = turrets.empty() ? 0.0f : SignedRadians(turrets[row].angle);
			const float pitch = turrets.empty() ? 0.0f : SignedRadians(turrets[row].pitch);
			const float altTurn = altTurrets.empty() ? 0.0f : SignedRadians(altTurrets[row].turret.angle);
			const float altPitch = altTurrets.empty() ? 0.0f : SignedRadians(altTurrets[row].turret.pitch);
			if (WeaponPose *weapon = weaponPoses.Get(entities[row]))
			{
				const std::uint32_t turning = (!turrets.empty() && turrets[row].rotating) || (!altTurrets.empty() && altTurrets[row].turret.rotating) ? 1u : 0u;
				*weapon = {weapon->currentTurret, turn, weapon->currentPitch, pitch, fired, fired != weapon->firedTick && fired != 0 ? 1u : 0u,
					armaments[row].firedBarrel, turning, 0, weapon->currentAltTurret, altTurn, weapon->currentAltPitch, altPitch};
			}
			else
				context.Commands().Add<WeaponPose>(entities[row],
					WeaponPose{turn, turn, pitch, pitch, fired, 0, armaments[row].firedBarrel, 0, 0, altTurn, altTurn, altPitch, altPitch});
			// Object::adjustModelConditionForWeaponStatus -> updateDrawableClipStatus (Weapon::getRemainingAmmo): each slot's
			// projectiles past what is left in its clip hidden; all of them while it reloads or is out of ammo.
			if (WeaponPose *weapon = weaponPoses.Get(entities[row]))
				for (std::size_t slot = 0; slot < weapon->projectilesHidden.size(); ++slot)
				{
					weapon->projectilesHidden[slot] = 0;
					const bool single = weaponSets.empty();
					if (single && slot != 0)
						break;
					const bool current = single || weaponSets[row].current == slot;
					const engine::gameplay::Armament &arm = armaments[row];
					const std::uint32_t id = current ? arm.weapon : weaponSets[row].slots[slot].weapon;
					if (id == engine::gameplay::WeaponCatalog::None)
						continue;
					const std::uint32_t clipSize = weaponCatalog.At(id).clipSize;
					if (clipSize == 0)
						continue;
					const std::uint64_t ready = current ? arm.readyTick : weaponSets[row].slots[slot].readyTick;
					const bool reloading = current ? arm.reloading : weaponSets[row].slots[slot].reloading;
					const std::uint32_t clip = current ? arm.clip : weaponSets[row].slots[slot].clip;
					const std::uint32_t loaded = engine::gameplay::RemainingAmmo(clipSize, clip, ready, reloading, tick);
					weapon->projectilesHidden[slot] = static_cast<std::uint8_t>(std::min<std::uint32_t>(clipSize - loaded, 254u));
				}
		}
	}
};

struct ObjectPresentationSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Optional<engine::gameplay::Owner>, ecs::Optional<engine::gameplay::Appearance>,
		ecs::Optional<engine::gameplay::Attitude>, ecs::Optional<engine::gameplay::ModelOverride>, ecs::Optional<engine::gameplay::Disabled>,
		ecs::Optional<engine::gameplay::DrawOffset>, ecs::Optional<engine::gameplay::Garrison>, ecs::Optional<engine::gameplay::ConstructionProgress>,
		ecs::Optional<engine::gameplay::Stealth>, ecs::Optional<engine::gameplay::DebrisLook>, ecs::Optional<engine::gameplay::Parachute>,
		ecs::Optional<engine::gameplay::ParachuteRider>, ecs::Optional<engine::gameplay::ObjectShroud>, ecs::Optional<engine::gameplay::OffMap>,
		ecs::Optional<engine::gameplay::PartOverrides>, ecs::Optional<engine::gameplay::Armament>, ecs::Optional<engine::gameplay::Sale>,
		ecs::Optional<engine::gameplay::StructureTopple>, ecs::Optional<gameplay::Railcar>, ecs::Optional<engine::gameplay::ResourceStore>,
		ecs::Optional<engine::gameplay::NeutronFlight>, ecs::Optional<engine::gameplay::DrawHidden>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Parachute>, ecs::Read<engine::gameplay::Dying>, ecs::Read<engine::gameplay::Mounted>,
		ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Appearance>,
		ecs::Read<engine::gameplay::EmpPulse>, ecs::Read<engine::gameplay::IndicatorColor>, ecs::Read<engine::gameplay::OcclusionSafe>>;
	using SideTables = ecs::SideTables<ecs::Read<TickPose>, ecs::Write<ShownLook>, ecs::Read<ObjectFade>, ecs::Read<TreadRoll>, ecs::Read<WheelRoll>, ecs::Read<WeaponPose>,
		ecs::Write<BarrelRecoil>, ecs::Read<ChassisMotion>, ecs::Write<TreeSway>, ecs::Read<TreeBend>, ecs::Write<HeatVision>,
		ecs::Read<DebrisMotion>, ecs::Write<ShroudSight>, ecs::Read<TintEnvelope>, ecs::Read<SelectionFlash>, ecs::Read<BeaconLook>, ecs::Write<ExtraShownLooks>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Read<LookCatalog>, ecs::Read<LookClips>, ecs::Read<engine::gameplay::Relationships>, ecs::Read<engine::gameplay::Detections>, ecs::Read<engine::gameplay::ParachuteCatalog>, ecs::Read<Breeze>, ecs::Read<TreeBreeze>, ecs::Read<BonePoses>, ecs::Write<ObjectInstances>, ecs::Write<MountedInstances>,
		ecs::Write<PresentedObjects>, ecs::Read<engine::gameplay::PlayerSciences>, ecs::Read<engine::gameplay::MatchOutcome>, ecs::Read<DetailSettings>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const
	{
		// Two slots past the chunks': the client's scenery (SceneryDrawSystem), then the mounted portable structures whose
		// carriers are drawn (MountedDrawSystem).
		context.Write<ObjectInstances>().Reset(query.PreparedChunkCount() + 2);
		context.Write<PresentedObjects>().Reset(query.PreparedChunkCount());
		context.Write<MountedInstances>().Reset(query.PreparedChunkCount());
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		using namespace object_presentation_detail;
		const PresentationFrame &frame = context.Read<PresentationFrame>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const LookClips &clips = context.Read<LookClips>();
		const Breeze &breeze = context.Read<Breeze>();
		const TreeBreeze &treeBreeze = context.Read<TreeBreeze>();
		const auto &relationships = context.Read<engine::gameplay::Relationships>();
		const auto &detections = context.Read<engine::gameplay::Detections>();
		auto &heats = context.Side<SideTables, HeatVision>();
		auto &sights = context.Side<SideTables, ShroudSight>();
		const auto &debrisMotions = context.SideRead<SideTables, DebrisMotion>();
		const auto &treeBends = context.SideRead<SideTables, TreeBend>();
		auto &sways = context.Side<SideTables, TreeSway>();
		auto &instances = context.Write<ObjectInstances>().Slot(context);
		auto &mountedInstances = context.Write<MountedInstances>().Slot(context);
		auto &presented = context.Write<PresentedObjects>().Slot(context);
		const auto &poses = context.SideRead<SideTables, TickPose>();
		auto &shownLooks = context.Side<SideTables, ShownLook>();
		auto &extraLooks = context.Side<SideTables, ExtraShownLooks>();
		const auto &treads = context.SideRead<SideTables, TreadRoll>();
		const auto &fades = context.SideRead<SideTables, ObjectFade>();
		const auto &wheels = context.SideRead<SideTables, WheelRoll>();
		const auto &weaponPoses = context.SideRead<SideTables, WeaponPose>();
		auto &recoils = context.Side<SideTables, BarrelRecoil>();
		const auto &chassisMotions = context.SideRead<SideTables, ChassisMotion>();
		const auto &tintEnvelopes = context.SideRead<SideTables, TintEnvelope>();
		const auto &selectionFlashes = context.SideRead<SideTables, SelectionFlash>();
		const auto &beaconLooks = context.SideRead<SideTables, BeaconLook>();
		const auto definitions = chunk.Get<engine::gameplay::DefinitionRef>();
		const auto owners = chunk.Get<engine::gameplay::Owner>();
		const auto appearances = chunk.Get<engine::gameplay::Appearance>();
		const auto attitudes = chunk.Get<engine::gameplay::Attitude>();
		const auto models = chunk.Get<engine::gameplay::ModelOverride>();
		const auto disabled = chunk.Get<engine::gameplay::Disabled>();
		const auto offsets = chunk.Get<engine::gameplay::DrawOffset>();
		const auto stealths = chunk.Get<engine::gameplay::Stealth>();
		const auto debrisLooks = chunk.Get<engine::gameplay::DebrisLook>();
		const auto chutes = chunk.Get<engine::gameplay::Parachute>();
		const auto chuteRiders = chunk.Get<engine::gameplay::ParachuteRider>();
		const auto railcars = chunk.Get<gameplay::Railcar>();
		const auto supplies = chunk.Get<engine::gameplay::ResourceStore>();
		const auto parachuteLookup = context.Lookup<Lookup>();
		const auto &parachuteCatalog = context.Read<engine::gameplay::ParachuteCatalog>();
		const auto garrisons = chunk.Get<engine::gameplay::Garrison>();
		const auto shrouds = chunk.Get<engine::gameplay::ObjectShroud>();
		const auto progress = chunk.Get<engine::gameplay::ConstructionProgress>();
		const auto away = chunk.Get<engine::gameplay::OffMap>();
		const auto partOverrides = chunk.Get<engine::gameplay::PartOverrides>();
		const auto armaments = chunk.Get<engine::gameplay::Armament>();
		const auto sales = chunk.Get<engine::gameplay::Sale>();
		const auto topples = chunk.Get<engine::gameplay::StructureTopple>();
		const auto neutrons = chunk.Get<engine::gameplay::NeutronFlight>();
		const auto drawHidden = chunk.Get<engine::gameplay::DrawHidden>();
		const BonePoses &bones = context.Read<BonePoses>();
		const auto entities = chunk.Entities();
		const float alpha = frame.alpha;
		const DetailSettings detail = context.Find<DetailSettings>() != nullptr ? *context.Find<DetailSettings>() : DetailSettings{};
		for (std::size_t row = 0; row < definitions.size(); ++row)
		{
			const ecs::Entity entity = entities[row];
			// Object::getIndicatorColor: a colour a script gave it, else its player's.
			const auto houseColor = [&](std::uint32_t owner) {
				if (const auto *custom = parachuteLookup.Get<engine::gameplay::IndicatorColor>(entity); custom != nullptr && custom->argb != 0)
					return std::array<float, 4>{static_cast<float>((custom->argb >> 16) & 0xFF) / 255.0f, static_cast<float>((custom->argb >> 8) & 0xFF) / 255.0f,
						static_cast<float>(custom->argb & 0xFF) / 255.0f, 1.0f};
				return catalog.ColorOf(owner);
			};
			if (!away.empty() && away[row].reason != MountedReason && away[row].reason != engine::gameplay::off_map_reason::Stationed)
				continue;
			const TickPose *pose = poses.Get(entity);
			// StealthUpdate::changeVisualDisguise: disguised, it is drawn as the disguise's definition (everyone sees it so).
			const engine::gameplay::Stealth *disguise =
				!stealths.empty() && stealths[row].shownAs != engine::gameplay::Stealth::NoDisguise ? &stealths[row] : nullptr;
			const std::uint32_t shownDefinition = disguise != nullptr ? disguise->shownAs : definitions[row].index;
			const DefinitionLooks *looks = catalog.Of(shownDefinition);
			if (pose == nullptr || looks == nullptr)
				continue; // not sampled or catalogued yet: from the next tick
			// Through the viewer's shroud (GameClient::update's setFullyObscuredByShroud, W3DScene's): fogged or shrouded it is
			// not drawn, for 2 seconds after it was last clear (5 more while dying) it still is; a ghost object stays in the fog
			// where the viewer is neutral to it or saw it before.
			// (A mounted portable structure shows as its carrier does: MountedDrawSystem.)
			const engine::gameplay::Mounted *mountedOn = away.empty() ? nullptr : parachuteLookup.Get<engine::gameplay::Mounted>(entity);
			if (mountedOn == nullptr && frame.viewer != PresentationFrame::NoViewer && !shrouds.empty() && frame.viewer < 64)
			{
				const engine::gameplay::ObjectShroud &shroud = shrouds[row];
				const bool clear = shroud.ClearTo(frame.viewer), seen = shroud.SeenBy(frame.viewer);
				const bool fogged = ((shroud.fogged >> frame.viewer) & 1u) != 0;
				ShroudSight *sight = sights.Get(entity);
				ShroudSight now = sight != nullptr ? *sight : ShroudSight{};
				if (clear)
					now.lastClear = frame.clock;
				if (seen)
					now.everSeen = 1;
				else if (!fogged)
					now.everSeen = 0; // wholly shrouded: forgotten
				if (sight != nullptr)
					*sight = now;
				else if (clear || seen)
					context.Commands().Add<ShroudSight>(entity, now);
				if (!seen)
				{
					const bool dying = appearances.empty() ? false : appearances[row].Test(catalog.bits.dying);
					const bool neutral = !owners.empty() && relationships.Between(frame.viewer, owners[row].player) == engine::gameplay::Relationship::Neutral;
					const bool ghost = looks->ghost && fogged && (neutral || (now.everSeen != 0 && !looks->mine));
					const bool lingering = frame.clock < now.lastClear + (dying ? 5.0 : 2.0);
					if (!ghost && !lingering)
						continue;
				}
			}
			engine::gameplay::Appearance appearance = appearances.empty() ? engine::gameplay::Appearance{} : appearances[row];
			if (catalog.night)
				appearance.Set(catalog.bits.night); // as the original's Drawable::setTimeOfDay
			if (catalog.snow)
				appearance.Set(catalog.bits.snow); // Object::setDrawable's MODELCONDITION_SNOW
			// GarrisonContain::getApparentControllingPlayer: a garrison hidden from the viewer looks empty and its
			// original player's.
			std::uint32_t player = owners.empty() ? 0u : owners[row].player;
			if (!garrisons.empty() && frame.viewer < 32 && ((garrisons[row].hiddenFrom >> frame.viewer) & 1u) != 0)
			{
				appearance.Set(content::ModelConditionBit("GARRISONED"), false);
				player = garrisons[row].originalPlayer;
			}
			// changeVisualDisguise's indicator colour: a player in the game not its ally sees the disguise's player's colour.
			if (disguise != nullptr && disguise->shownPlayer >= 0 && frame.viewer != PresentationFrame::NoViewer && !owners.empty() &&
				!relationships.Allies(owners[row].player, frame.viewer))
				player = static_cast<std::uint32_t>(disguise->shownPlayer);
			const auto state = static_cast<std::uint32_t>(looks->states.Empty() ? 0 : content::SelectModelState(looks->states, appearance.flags));
			const std::uint32_t model = models.empty() ? 0u : models[row].model;
			const std::uint32_t stateIndex = state < looks->stateLooks.size() ? state : 0;
			std::uint32_t look = model != 0 ? catalog.LookOfModel(model) : looks->stateLooks[stateIndex];
			// A debris piece's animation (DebrisAnimationSystem), from when it started.
			const DebrisMotion *debrisMotion = model != 0 ? debrisMotions.Get(entity) : nullptr;
			if (debrisMotion != nullptr && debrisMotion->animation != 0)
				look = catalog.LookOfModel(model, debrisMotion->animation, debrisMotion->mode);
			// Its draw's model states (W3DModelDraw), for its own model: the state showing may be another
			// than its conditions pick for a while (a transition, an animation finishing first).
			const bool stateful = model == 0 && !looks->states.Empty();
			const ModelStateRolls rolls{Key(entity), frame.frame};
			std::uint32_t shownState = state;
			if (stateful)
				if (ShownLook *shown = shownLooks.Get(entity))
				{
					// A new drawable for another definition (a disguise taken or lost): its model states start over, in its colour.
					if (shown->definition != shownDefinition && shown->definition != 0xFFFFFFFFu)
					{
						*shown = ShownLook{};
						ShowModelState(*shown, *looks, clips, stateIndex, frame.clock, rolls, false);
						shown->state = stateIndex;
						shown->madeColor = houseColor(player);
					}
					shown->definition = shownDefinition;
					StepModelState(*shown, *looks, clips, stateIndex, frame.clock, rolls);
					SettlePoliceLights(*shown, *looks, clips, frame.clock, rolls);
					// Its walk or run matched to how far it went in the last tick.
					const float dx = pose->current[0] - pose->previous[0], dy = pose->current[1] - pose->previous[1],
								dz = pose->current[2] - pose->previous[2];
					MatchMovementSpeed(*shown, *looks, clips, std::sqrt(dx * dx + dy * dy + dz * dz), frame.clock);
					// setAnimationLoopDuration as the logic asks: a weapon winding up (Object::adjustModelConditionForWeaponStatus,
					// PREATTACK: its animation takes what is left of the wind-up) and a sale (BuildAssistant::sellObject, and again as
					// it turns SOLD: TOTAL_FRAMES_TO_SELL_OBJECT / 2, 45 frames), each once as it comes on.
					namespace mc = content::model_condition;
					const bool windingUp = appearance.Test(mc::PreattackA) || appearance.Test(mc::PreattackA + 4) || appearance.Test(mc::PreattackA + 8);
					const std::uint8_t on = static_cast<std::uint8_t>((windingUp ? 1u : 0u) | (!sales.empty() ? 2u : 0u) | (appearance.Test(mc::Sold) ? 4u : 0u));
					const std::uint8_t rising = static_cast<std::uint8_t>(on & ~shown->stretched);
					shown->stretched = on;
					if ((rising & 1u) != 0 && !armaments.empty() && armaments[row].preAttackUntil > frame.tick)
						StretchToFrames(*shown, *looks, clips, armaments[row].preAttackUntil - frame.tick, frame.clock);
					if ((rising & 6u) != 0)
						StretchToFrames(*shown, *looks, clips, 45, frame.clock);
					look = shown->look;
					shownState = shown->shown;
				}
			// The look its animation runs in: the parts and supply shown below are the same model and animation drawn otherwise
			// (W3DModelDraw::showSubObject on the render object it has), never a new look to start over in.
			const std::uint32_t shownLook = look;
			// Its parts as its upgrades left them (SubObjectsUpgrade): its look's copy drawn with them (not a disguise's drawable:
			// changeVisualDisguise loses them).
			if (disguise == nullptr && !partOverrides.empty() && partOverrides[row].count != 0)
				look = looks->WithParts(look, std::span<const std::uint8_t>(partOverrides[row].applied.data(), partOverrides[row].count));
			// W3DSupplyDraw: its supply bones shown as its stock stands.
			else if (!supplies.empty() && looks->supplyBones > 0)
				look = looks->WithSupply(look, looks->SupplyShown(supplies[row].boxes, supplies[row].startingBoxes));
			if (look >= catalog.looks.size())
				continue;

			// Between the last two ticks.
			std::array<float, 3> at{};
			for (std::size_t axis = 0; axis < 3; ++axis)
				at[axis] = pose->previous[axis] + (pose->current[axis] - pose->previous[axis]) * alpha;
			// Drawn off where it is (a collapsing structure: sunk, shuddering a new way each frame, as the original's
			// StructureCollapseUpdate on the client's random numbers).
			if (!offsets.empty())
			{
				at[2] += Engine::Math::ToFloat(offsets[row].z);
				if (const float shudder = Engine::Math::ToFloat(offsets[row].shudder); shudder > 0.0f)
				{
					const std::uint32_t mixed = ModelStateRolls::Mix(Key(entity) ^ (static_cast<std::uint64_t>(frame.frame) * 0x9E3779B97F4A7C15ull));
					at[0] += shudder * (static_cast<float>(mixed & 0xFFFFu) / 32767.5f - 1.0f);
					at[1] += shudder * (static_cast<float>(mixed >> 16) / 32767.5f - 1.0f);
				}
			}
			// A superweapon missile shaking as its climb begins (NeutronMissileUpdate::doAttack's instance transform, set each tick).
			if (!neutrons.empty() && looks->neutronJitter > 0.0f && neutrons[row].state == engine::gameplay::NeutronState::Attack &&
				frame.tick > neutrons[row].launchTick)
			{
				const auto &forward = neutrons[row].forward;
				const std::uint32_t roll = ModelStateRolls::Mix(Key(entity) ^ (frame.tick * 0xC2B2AE3D27D4EB4Full));
				const auto shake = NeutronJitter(looks->neutronJitter, looks->neutronSpecialTicks, frame.tick - neutrons[row].launchTick,
					{Engine::Math::ToFloat(forward.x), Engine::Math::ToFloat(forward.y), Engine::Math::ToFloat(forward.z)},
					static_cast<float>(pose->currentFacing) * RadiansPerUnit, roll);
				for (std::size_t axis = 0; axis < 3; ++axis)
					at[axis] += shake[axis];
			}
			// W3DModelDraw::adjustTransformMtx (ADJUST_HEIGHT_BY_CONSTRUCTION_PERCENT): sunk by what is left to build, per draw
			// module by its own state's flag (getConstructionPercent: below zero once built).
			const float constructionSink = !progress.empty() && Engine::Math::ToFloat(progress[row].percent) >= 0.0f
				? -looks->constructionHeight + looks->constructionHeight * Engine::Math::ToFloat(progress[row].percent) / 100.0f : 0.0f;
			const float ownSink = shownState < looks->states.states.size() && looks->states.states[shownState].adjustHeightByConstruction ? constructionSink : 0.0f;
			at[2] += ownSink;
			const auto turn = static_cast<std::int32_t>(pose->currentFacing - pose->previousFacing);
			float facing = static_cast<float>(pose->previousFacing) * RadiansPerUnit + static_cast<float>(turn) * RadiansPerUnit * alpha;
			// W3DDependencyModelDraw: a portable structure mounted on its carrier is drawn on the carrier's
			// AttachToBoneInContainer bone as the carrier's model poses it (turned and pitched with its turret).
			if (!away.empty() && !looks->attachToBone.empty())
				if (const auto *mounted = parachuteLookup.Get<engine::gameplay::Mounted>(entity))
				{
					const auto *carrierRef = parachuteLookup.Get<engine::gameplay::DefinitionRef>(mounted->carrier);
					const DefinitionLooks *carrierLooks = carrierRef != nullptr ? catalog.Of(carrierRef->index) : nullptr;
					if (carrierLooks != nullptr && !carrierLooks->states.Empty())
					{
						const auto *carrierLook = parachuteLookup.Get<engine::gameplay::Appearance>(mounted->carrier);
						const auto carrierState = static_cast<std::uint32_t>(
							content::SelectModelState(carrierLooks->states, carrierLook != nullptr ? carrierLook->flags : engine::gameplay::Appearance{}.flags));
						const std::uint32_t carrierIndex = carrierState < carrierLooks->stateLooks.size() ? carrierState : 0;
						const std::uint32_t carrierModel = carrierLooks->stateLooks[carrierIndex];
						const WeaponPose *aim = weaponPoses.Get(mounted->carrier);
						const float turret = aim == nullptr ? 0.0f : Between(aim->previousTurret, aim->currentTurret, alpha);
						const float pitch = aim == nullptr ? 0.0f : aim->previousPitch + (aim->currentPitch - aim->previousPitch) * alpha;
						PresentedObject carrier{};
						carrier.position = at;
						carrier.facing = facing;
						carrier.scale = carrierLooks->scale;
						if (carrierModel < catalog.lookModels.size() && carrierIndex < carrierLooks->states.states.size())
							if (const auto place = PlaceOnBarrel(carrier, carrierLooks->states.states[carrierIndex], catalog.lookModels[carrierModel], bones, turret,
									pitch, 0u, nullptr, looks->attachToBone))
							{
								at = place->at;
								facing = place->yaw;
							}
					}
				}

			// Its animation runs from when it took this look (objects seen together start out of step).
			double since = frame.clock;
			// Drawable::getShouldAnimate: paused while disabled this way (helicopters keep animating).
			namespace dt = engine::gameplay::disabled_type;
			const std::uint32_t off = disabled.empty() ? 0u : disabled[row].mask;
			const std::uint32_t pausing = dt::Hacked | dt::Paralyzed | dt::Emp | dt::Subdued | dt::Unmanned |
				(looks->animationsRequirePower ? dt::Underpowered : 0u);
			const bool paused = !looks->animatesWhileDisabled && (off & pausing) != 0;
			// A look without model states (a model shown instead of its own) runs from when it took it.
			const auto begin = [&](double at) { return ShownLook{at, shownLook, 1.0f, 0.0f, -1.0}; };
			float speed = 1.0f, start = 0.0f;
			std::array<float, 4> madeColor{1, 1, 1, 0};
			if (ShownLook *shown = shownLooks.Get(entity))
			{
				if (shown->look != shownLook)
				{
					const std::array<float, 4> made = shown->madeColor;
					const std::uint8_t stretched = shown->stretched;
					*shown = begin(frame.clock);
					shown->madeColor = made;
					shown->stretched = stretched;
				}
				if (paused && shown->held < 0.0)
					shown->held = frame.clock - shown->since;
				if (shown->held >= 0.0)
				{
					// Holding its frame: its start moves with the clock (from where it held, once resumed).
					shown->since = frame.clock - shown->held;
					if (!paused)
						shown->held = -1.0;
				}
				since = shown->since;
				speed = shown->speed;
				start = shown->start;
				madeColor = shown->madeColor;
				if (debrisMotion != nullptr && debrisMotion->animation != 0)
					since = shown->since = debrisMotion->since;
			}
			else
			{
				since = frame.clock - static_cast<double>(Key(entity) * 2654435761u % 997) / 97.0;
				ShownLook fresh = begin(since);
				if (stateful)
				{
					fresh = ShownLook{};
					ShowModelState(fresh, *looks, clips, stateIndex, since, rolls, false);
					fresh.state = stateIndex;
					look = fresh.look;
				}
				speed = fresh.speed;
				start = fresh.start;
				fresh.madeColor = houseColor(player);
				fresh.definition = shownDefinition;
				madeColor = fresh.madeColor;
				context.Commands().Add<ShownLook>(entity, fresh);
			}

			// Rz(facing) Ry(pitch) Rx(roll), scaled.
			float scale = looks->scale;
			// EMPUpdate: an EMP pulse grows and fades its tint.
			std::optional<EmpLook> empLook;
			if (looks->emp)
				if (const auto *pulse = parachuteLookup.Get<engine::gameplay::EmpPulse>(entity))
				{
					empLook = EmpPulseLook(*looks, Engine::Math::ToFloat(pulse->targetScale), pulse->fadeTick, pulse->dieTick,
						static_cast<double>(frame.tick) - 1.0 + static_cast<double>(alpha));
					scale *= empLook->scale;
				}
			const float c = std::cos(facing), s = std::sin(facing);
			const engine::gameplay::Attitude attitude = attitudes.empty() ? engine::gameplay::Attitude{} : attitudes[row];
			float pitch = static_cast<float>(static_cast<std::int32_t>(attitude.pitch.units)) * RadiansPerUnit;
			float roll = static_cast<float>(static_cast<std::int32_t>(attitude.roll.units)) * RadiansPerUnit;
			// FloatUpdate: rocking like a buoy, its turn kept (Rz, then Ry by sin(frame * 0.0291) * 0.05, Rx by
			// sin(frame * 0.0515) * 0.05, on the game clock in frames of 1/30 s).
			// SwayClientUpdate: its sway rolled for the breeze (each tree its own share of the breeze's randomness), on
			// round at the breeze's rate (a logic frame's worth per 1/30 s), leaning cos(value) * limit + lean along the
			// breeze in its own frame; a burned tree stands where it stopped.
			if (looks->treeSway)
			{
				if (TreeSway *sway = sways.Get(entity))
				{
					if (sway->version != breeze.version)
					{
						const float spread = breeze.randomness * 0.5f;
						const auto roll = [&](std::uint64_t salt) {
							const std::uint32_t mixed = ModelStateRolls::Mix(Key(entity) ^ (salt * 0x9E3779B97F4A7C15ull) ^ static_cast<std::uint64_t>(breeze.version));
							return 1.0f - spread + 2.0f * spread * static_cast<float>(mixed % 10001u) / 10000.0f;
						};
						if (breeze.randomness == 0.0f)
							sway->value = 0.0f;
						sway->limit = breeze.intensity * roll(1);
						sway->delta = 2.0f * std::numbers::pi_v<float> / breeze.periodFrames * roll(2);
						sway->lean = breeze.lean * roll(3);
						sway->version = breeze.version;
					}
					if (sway->swaying != 0)
					{
						sway->value = std::fmod(sway->value + sway->delta * frame.seconds * 30.0f, 2.0f * std::numbers::pi_v<float>);
						sway->angle = std::cos(sway->value) * sway->limit + sway->lean;
						constexpr std::uint32_t smoldering = content::ModelConditionBit("SMOLDERING");
						if (appearance.Test(smoldering))
							sway->swaying = 0;
					}
					pitch += sway->angle * breeze.directionY;
					roll += -sway->angle * breeze.directionX;
				}
				// Drawable's constructor: shrubbery makes no SwayClientUpdate while tree sway is off.
				else if (!looks->shrubbery || detail.useTreeSway)
					context.Commands().Add<TreeSway>(entity, TreeSway{});
			}
			if (looks->sways)
			{
				const double frames = frame.clock * 30.0;
				pitch = static_cast<float>(std::sin(frames * 0.0291) * 0.05);
				roll = static_cast<float>(std::sin(frames * 0.0515) * 0.05);
			}
			const float cp = std::cos(pitch), sp = std::sin(pitch), cr = std::cos(roll), sr = std::sin(roll);
			std::array<std::array<float, 3>, 3> m{{{c * cp, c * sp * sr - s * cr, c * sp * cr + s * sr}, {s * cp, s * sp * sr + c * cr, s * sp * cr - c * sr},
				{-sp, cp * sr, cp * cr}}};
			// StructureToppleUpdate: leaning over its fall, in the world's frame about its position (In_Place_Pre_Rotate X by
			// -v dir.y, Y by v dir.x each tick: a turn about (-dir.y, dir.x, 0) by every velocity applied), between the last
			// two ticks.
			if (!topples.empty() && topples[row].state == engine::gameplay::StructureToppleState::Toppling)
			{
				const engine::gameplay::StructureTopple &topple = topples[row];
				constexpr double q32 = 4294967296.0;
				const float lean = static_cast<float>((static_cast<double>(topple.PreviousLean()) +
					static_cast<double>(topple.Lean() - topple.PreviousLean()) * static_cast<double>(alpha)) / q32);
				const float ax = -Engine::Math::ToFloat(topple.direction.y), ay = Engine::Math::ToFloat(topple.direction.x);
				const float lc = std::cos(lean), ls = std::sin(lean), lt = 1.0f - lc;
				const std::array<std::array<float, 3>, 3> tilt{{{lc + ax * ax * lt, ax * ay * lt, ay * ls}, {ax * ay * lt, lc + ay * ay * lt, -ax * ls},
					{-ay * ls, ax * ls, lc}}};
				std::array<std::array<float, 3>, 3> product{};
				for (std::size_t i = 0; i < 3; ++i)
					for (std::size_t j = 0; j < 3; ++j)
						for (std::size_t k = 0; k < 3; ++k)
							product[i][j] += tilt[i][k] * m[k][j];
				m = product;
			}
			// Its body rocking on its suspension (Drawable::applyPhysicsXform, between the last two ticks):
			// raised by z, then Ry(pitch) Rx(-roll) Rz(yaw) in its own frame.
			std::array<float, 4> suspension{};
			if (const ChassisMotion *chassis = chassisMotions.Get(entity); chassis != nullptr && chassis->samples > 1)
			{
				for (std::size_t wheel = 0; wheel < 4; ++wheel)
					suspension[wheel] = chassis->previous.wheels[wheel] + (chassis->current.wheels[wheel] - chassis->previous.wheels[wheel]) * alpha;
				const auto mix = [&](float from, float to) { return from + (to - from) * alpha; };
				const float bodyPitch = mix(chassis->previous.pitch, chassis->current.pitch), bodyRoll = -mix(chassis->previous.roll, chassis->current.roll);
				const float bodyYaw = mix(chassis->previous.yaw, chassis->current.yaw), lift = mix(chassis->previous.z, chassis->current.z);
				for (std::size_t axis = 0; axis < 3; ++axis)
					at[axis] += m[axis][2] * lift;
				const float bp = std::cos(bodyPitch), bsp = std::sin(bodyPitch), br = std::cos(bodyRoll), bsr = std::sin(bodyRoll);
				const float by = std::cos(bodyYaw), bsy = std::sin(bodyYaw);
				// Ry(p) Rx(r) Rz(y)
				const std::array<std::array<float, 3>, 3> body{{{bp * by + bsp * bsr * bsy, -bp * bsy + bsp * bsr * by, bsp * br},
					{br * bsy, br * by, -bsr}, {-bsp * by + bp * bsr * bsy, bsp * bsy + bp * bsr * by, bp * br}}};
				std::array<std::array<float, 3>, 3> product{};
				for (std::size_t i = 0; i < 3; ++i)
					for (std::size_t j = 0; j < 3; ++j)
						for (std::size_t k = 0; k < 3; ++k)
							product[i][j] += m[i][k] * body[k][j];
				m = product;
			}
			// ParachuteContain: a chute is hidden until it opens (setDrawableHidden); open, it and its held rider swing by its
			// pitch and roll about its sway centre and the rider's (calcSwayTransform: the drawable's instance transform).
			const engine::gameplay::Parachute *swaying = nullptr;
			std::array<float, 3> pivot{};
			const auto facingOf = [](float radians) {
				return Engine::Math::TurnAngle{static_cast<std::uint32_t>(static_cast<std::int64_t>(std::llround(radians / RadiansPerUnit)))};
			};
			// Hidden by the simulation (setDrawableHidden: a guided missile gone off, holding its KILL_SELF state).
			if (!drawHidden.empty())
				continue;
			// RailroadBehavior: a train car in the wings or past the end of the line is hidden (setDrawableHidden).
			if (!railcars.empty() && railcars[row].hidden != 0)
				continue;
			// BeaconClientUpdate::hideBeacon: a beacon this client does not show (setDrawableHidden).
			if (const BeaconLook *beacon = beaconLooks.Get(entity); beacon != nullptr && beacon->hidden != 0)
				continue;
			// W3DScienceModelDraw: hidden from a viewer still playing without its RequiredScience (an observer, or one
			// defeated, sees it); an unknown science hides it from everyone.
			if (looks->needsScience)
			{
				bool shown = looks->requiredScience != 0xFFFFFFFFu;
				if (shown && frame.viewer != PresentationFrame::NoViewer && !context.Read<engine::gameplay::PlayerSciences>().Has(frame.viewer, looks->requiredScience))
				{
					bool active = true;
					for (const auto &standing : context.Read<engine::gameplay::MatchOutcome>().players)
						if (standing.player == frame.viewer && standing.defeated)
							active = false;
					shown = !active;
				}
				if (!shown)
					continue;
			}
			if (!chutes.empty())
			{
				if (!chutes[row].Has(engine::gameplay::parachute_flag::Opened))
					continue;
				swaying = &chutes[row];
				const auto offsets = engine::gameplay::RigParachute(parachuteCatalog.At(swaying->definition), *swaying, facingOf(facing));
				pivot = {Engine::Math::ToFloat(offsets.paraSway.x), Engine::Math::ToFloat(offsets.paraSway.y), Engine::Math::ToFloat(offsets.paraSway.z)};
			}
			else if (!chuteRiders.empty())
				if (const engine::gameplay::Parachute *chute = parachuteLookup.Get<engine::gameplay::Parachute>(chuteRiders[row].chute))
				{
					swaying = chute;
					const auto offsets = engine::gameplay::RigParachute(parachuteCatalog.At(chute->definition), *chute, facingOf(facing));
					pivot = {Engine::Math::ToFloat(offsets.riderSway.x), Engine::Math::ToFloat(offsets.riderSway.y), Engine::Math::ToFloat(offsets.riderSway.z)};
				}
			if (swaying != nullptr && (swaying->pitch != 0 || swaying->roll != 0))
			{
				const float sp = std::sin(static_cast<float>(swaying->pitch) * RadiansPerUnit), cp = std::cos(static_cast<float>(swaying->pitch) * RadiansPerUnit);
				const float sr = std::sin(static_cast<float>(swaying->roll) * RadiansPerUnit), cr = std::cos(static_cast<float>(swaying->roll) * RadiansPerUnit);
				// Ry(pitch) Rx(roll), about the pivot: T(p) R T(-p).
				const std::array<std::array<float, 3>, 3> sway{{{cp, sp * sr, sp * cr}, {0.0f, cr, -sr}, {-sp, cp * sr, cp * cr}}};
				std::array<float, 3> shift{};
				for (std::size_t i = 0; i < 3; ++i)
					shift[i] = pivot[i] - (sway[i][0] * pivot[0] + sway[i][1] * pivot[1] + sway[i][2] * pivot[2]);
				std::array<std::array<float, 3>, 3> product{};
				for (std::size_t i = 0; i < 3; ++i)
				{
					for (std::size_t j = 0; j < 3; ++j)
						for (std::size_t k = 0; k < 3; ++k)
							product[i][j] += m[i][k] * sway[k][j];
					at[i] += m[i][0] * shift[0] + m[i][1] * shift[1] + m[i][2] * shift[2];
				}
				m = product;
			}
			ObjectInstance instance{look,
				{m[0][0] * scale, m[0][1] * scale, m[0][2] * scale, at[0], m[1][0] * scale, m[1][1] * scale, m[1][2] * scale, at[1], m[2][0] * scale,
					m[2][1] * scale, m[2][2] * scale, at[2], 0, 0, 0, 1},
				// W3DDebrisDraw::setModelName: a debris piece shows its player's colour only when its list lets it (OkToChangeModelColor).
				// A model draw keeps the colour it was made with unless it may change it (W3DModelDraw::replaceIndicatorColor:
				// a capture, a script's colour).
				!debrisLooks.empty() && model != 0 ? (debrisLooks[row].playerColor == 0 ? std::array<float, 4>{1, 1, 1, 0} : houseColor(player))
					: looks->states.okToChangeColor ? houseColor(player) : madeColor,
				static_cast<float>((frame.clock - since) * speed), appearance.Test(catalog.bits.firing)};
			instance.animationStart = start;
			instance.key = Key(entity);
			instance.clipLook = shownLook;
			// Its colour tint (TintEnvelope: disabled), or an EMP pulse's own.
			if (const TintEnvelope *tint = tintEnvelopes.Get(entity); tint != nullptr && tint->affect != 0)
				instance.tint = tint->current;
			// Its selection flash on top (W3DScene: the tint and the selection colour summed into its lights).
			if (const SelectionFlash *flash = selectionFlashes.Get(entity); flash != nullptr && flash->envelope.affect != 0)
				for (std::size_t channel = 0; channel < 3; ++channel)
					instance.tint[channel] += flash->envelope.current[channel];
			if (empLook)
				instance.tint = empLook->tint;
			// A map tree bent by the breeze about its base (W3DTreeBuffer: its sway type rolled as it was added).
			// Toppled or leaning first (the tree buffer's vertices), then the breeze on top (the tree shader); one sunk away
			// is no longer drawn.
			const TreeBend *treeBend = looks->bufferTree ? treeBends.Get(entity) : nullptr;
			if (treeBend != nullptr)
			{
				BentTree(instance.world, *treeBend, looks->treeMotion);
				// Pushed aside, it darkens (W3DTreeBuffer: sway.y = 1 - DarkeningFactor x pushAside).
				instance.shade = 1.0f - looks->treeMotion.darkening * treeBend->pushAside;
			}
			if (looks->bufferTree)
				ShearTree(instance.world, treeBreeze.current[TreeSwayType(tree_breeze_sway_detail::Unit(Key(entity)))], at[2]);
			instance.night = appearance.Test(catalog.bits.night);
			// Its turret as aimed between the last two ticks; its flashes the tick it fired (FiringA without a weapon pose).
			if (const WeaponPose *weapon = weaponPoses.Get(entity))
			{
				instance.turret = Between(weapon->previousTurret, weapon->currentTurret, alpha);
				instance.turretPitch = Between(weapon->previousPitch, weapon->currentPitch, alpha);
				instance.altTurret = Between(weapon->previousAltTurret, weapon->currentAltTurret, alpha);
				instance.altTurretPitch = Between(weapon->previousAltPitch, weapon->currentAltPitch, alpha);
				instance.muzzleFlash = weapon->flash != 0;
				instance.projectilesHidden = weapon->projectilesHidden;
				// Its barrels: a new shot starts its barrel; the flashes shown are those
				// starting this frame (before the step), the recoil where the step leaves it.
				if (BarrelRecoil *recoil = recoils.Get(entity))
				{
					const RecoilMotion &motion = looks->recoil;
					if (weapon->firedTick != recoil->firedTick && weapon->firedTick != 0)
					{
						StartRecoil(*recoil, weapon->barrel % BarrelRecoil::MaxBarrels, motion);
						recoil->firedTick = weapon->firedTick;
					}
					std::uint8_t flashing = 0;
					for (std::uint32_t barrel = 0; barrel < BarrelRecoil::MaxBarrels; ++barrel)
						flashing |= recoil->state[barrel] == BarrelRecoil::Start ? static_cast<std::uint8_t>(1u << barrel) : 0u;
					recoil->frames += frame.seconds * 30.0f;
					for (; recoil->frames >= 1.0f; recoil->frames -= 1.0f)
						for (std::uint32_t barrel = 0; barrel < BarrelRecoil::MaxBarrels; ++barrel)
							StepRecoil(*recoil, barrel, motion);
					instance.flashBarrels = flashing;
					instance.muzzleFlash = flashing != 0;
					instance.recoil = recoil->shift;
				}
				else
					context.Commands().Add<BarrelRecoil>(entity, BarrelRecoil{{}, {}, {}, weapon->firedTick, 0.0f, 0});
			}
			// Its shadow (W3DModelDraw's), except while it is detected in stealth (Drawable::updateDrawable:
			// setShadowsEnabled(look != STEALTHLOOK_VISIBLE_DETECTED)), or once it is infantry dying a death that sinks it
			// (SlowDeathBehavior::beginSlowDeath: no floating shadow over the sunk body), or once toppling (ToppleUpdate::update:
			// setShadowsEnabled(false) from its first fall on), or shrubbery burned by a blast's scorch wave
			// (NeutronMissileSlowDeathBehavior::doScorchBlast), or once a flood wave flooded it (WaveGuideUpdate::doDamage:
			// MODELCONDITION_FLOODED and setShadowsEnabled(FALSE), for good).
			const engine::gameplay::Dying *dying = looks->infantry ? parachuteLookup.Get<engine::gameplay::Dying>(entity) : nullptr;
			const bool sinks = dying != nullptr && dying->sinkRate > Engine::Math::Fixed{};
			// EA's W3DVolumetricShadow / W3DProjectedShadow: a SHADOW_VOLUME draws only while UseShadowVolumes is on, a
			// decal or projection only while UseShadowDecals is.
			const bool shadowKindOn = looks->shadowKind == 0 || (looks->shadowKind == 2u ? detail.useShadowVolumes : detail.useShadowDecals);
			instance.castsShadow = looks->castsShadow && shadowKindOn && !sinks && !(appearance.Test(catalog.bits.stealthed) && appearance.Test(catalog.bits.detected)) &&
				!appearance.Test(catalog.bits.toppled) && !(looks->shrubbery && appearance.Test(catalog.bits.burned)) &&
				!appearance.Test(content::ModelConditionBit("FLOODED"));
			// A SHADOW_DECAL's is its texture laid on the terrain (W3DProjectedShadowManager's decal list), not a cast one.
			instance.shadowDecal = instance.castsShadow ? looks->shadowDecal : DefinitionLooks::NoShadowDecal;
			if (looks->shadowDecal != DefinitionLooks::NoShadowDecal)
				instance.castsShadow = false;
			instance.receivesDynamicLights = looks->receivesDynamicLights;
			instance.infantry = looks->infantry;
			instance.lightSphere = {at[0], at[1], at[2] + looks->constructionHeight * 0.5f, looks->lightRadius};
			// StealthUpdate::calcStealthedStatusForPlayer: stealthed and not detected, it is seen through (pulsing) by the
			// viewer's allies (by everyone, when nobody's eyes: an observer) and not seen at all by anyone else.
			const bool hiddenByStealth = appearance.Test(catalog.bits.stealthed) && !appearance.Test(catalog.bits.detected) &&
				frame.viewer != PresentationFrame::NoViewer && !owners.empty() && !relationships.Allies(owners[row].player, frame.viewer);
			// Its heat vision (StealthUpdate::calcStealthedStatusForPlayer, Drawable::setStealthLook and draw): a look turning
			// detected glows at once (not a mine); so does each detector scan that finds it; the glow fades by 0.8 a logic
			// frame until it is under 0.001, and a dead thing shows none. An enemy's view of a detected thing is the glow only.
			const bool stealthed = appearance.Test(catalog.bits.stealthed), detected = appearance.Test(catalog.bits.detected);
			const bool friendly = frame.viewer == PresentationFrame::NoViewer || owners.empty() || relationships.Allies(owners[row].player, frame.viewer);
			const std::uint32_t stealthLook = appearance.Test(catalog.bits.dying) || !stealthed ? 0u : detected ? (friendly ? 2u : 3u) : (friendly ? 1u : 4u);
			if (HeatVision *heat = heats.Get(entity))
			{
				if (heat->look != stealthLook)
				{
					heat->opacity = (stealthLook == 2u || stealthLook == 3u) && !looks->mine ? 1.0f : 0.0f;
					heat->look = stealthLook;
				}
				if (const std::uint64_t until = detections.For(entity); until != 0 && until != heat->detection)
				{
					heat->detection = until;
					if (!looks->mine)
						heat->opacity = 1.0f;
				}
				// StealthUpdate::hintDetectableWhileUnstealthed: kept from stealth in a hint condition, it flashes for its own player.
				if (!stealths.empty() && stealths[row].Has(engine::gameplay::stealth_flag::HintDetectable) && !owners.empty() &&
					owners[row].player == frame.viewer)
					heat->opacity = 1.0f;
				if (appearance.Test(catalog.bits.dying))
					heat->opacity = 0.0f;
				else if (heat->opacity > 0.001f)
					heat->opacity *= 1.0f - (1.0f - 0.8f) * frame.seconds * 30.0f;
				else
					heat->opacity = 0.0f;
				instance.heatVision = std::max(heat->opacity, 0.0f);
				instance.heatOnly = stealthLook == 3u;
			}
			else if (stealthed || (!stealths.empty() && stealths[row].Has(engine::gameplay::stealth_flag::HintDetectable)))
				context.Commands().Add<HeatVision>(entity, HeatVision{});
			if (appearance.Test(catalog.bits.stealthed) && !appearance.Test(catalog.bits.detected))
			{
				if (looks->stealth)
				{
					const double phase = frame.clock * 30.0 / static_cast<double>(looks->stealthPulseTicks) * 6.283185307179586;
					const float pulse = 0.5f + 0.5f * static_cast<float>(std::sin(phase));
					instance.opacity = looks->stealthMin + (looks->stealthMax - looks->stealthMin) * pulse;
				}
				else
					instance.opacity = 0.5f;
			}
			// StealthUpdate::update's disguise transition (setEffectiveOpacity(|1 - 2 factor|)): fading out to its halfway point
			// and in again, factor = 1 - ticks left / its transition's.
			if (!stealths.empty() && stealths[row].transitionLeft != 0)
			{
				const engine::gameplay::Stealth &shifting = stealths[row];
				const std::uint32_t total = shifting.Has(engine::gameplay::stealth_flag::ToDisguise) ? shifting.disguiseTicks : shifting.revealTicks;
				if (total != 0)
				{
					const float factor = 1.0f - static_cast<float>(shifting.transitionLeft) / static_cast<float>(total);
					instance.opacity = std::abs(1.0f - factor * 2.0f);
				}
			}
			// Its drawable's fade (Drawable::fadeIn / fadeOut), over whatever opacity its stealth gives it.
			if (const ObjectFade *fade = fades.Get(entity))
				instance.opacity *= FadeOpacity(*fade, frame.clock);
			if (const TreadRoll *tread = treads.Get(entity))
				instance.treads = tread->offsets;
			if (const WheelRoll *wheel = wheels.Get(entity))
			{
				instance.wheels = wheel->angle;
				instance.rearWheels = wheel->rearAngle;
				instance.suspension = suspension;
				instance.steer = wheel->steer;
				instance.cab = wheel->cab;
				instance.trailer = wheel->trailer;
			}
			// RTS3DScene::renderOneObject's sorting for building occlusion (whether it is on is the renderer's): a structure
			// seen whole may hide what is behind it (a potential occluder; a translucent one is not); a scoring thing past its
			// safe occlusion frame (OcclusionDelay after it was made) shows through it in its player's colour.
			if (looks->structure)
				instance.occlusion = instance.opacity >= 1.0f ? 1u : 0u;
			else if (looks->scoring && !owners.empty())
				if (const auto *safe = parachuteLookup.Get<engine::gameplay::OcclusionSafe>(entity); safe != nullptr && safe->tick <= frame.tick)
				{
					instance.occlusion = 2u;
					instance.occludedPlayer = owners[row].player;
					instance.occludedColor = catalog.ColorOf(owners[row].player); // getControllingPlayer()->getPlayerColor()
				}
			const bool drawn = (treeBend == nullptr || treeBend->state != tree_bend_state::Gone) && !hiddenByStealth;
			const auto emit = [&](const ObjectInstance &shown) {
				if (mountedOn != nullptr)
					mountedInstances.push_back({Key(mountedOn->carrier), shown});
				else
					instances.push_back(shown);
			};
			if (drawn)
				emit(instance);
			// Its other draw modules (a W3DModelDraw each): each made in the state no conditions pick (the constructor's
			// findBestInfo(emptyFlags)), then stepping to the state its conditions pick as its own draw does (a transition
			// first: a scaffold rising), paused as the object is, sunk by its own state's flag; drawn where it is unless that
			// state shows no model.
			if (model == 0 && !looks->extraDraws.empty())
			{
				ExtraShownLooks *kept = extraLooks.Get(entity);
				ExtraShownLooks made;
				const bool fresh = kept == nullptr || kept->definition != shownDefinition;
				ExtraShownLooks &draws = kept != nullptr ? *kept : made;
				if (fresh)
				{
					draws = ExtraShownLooks{};
					draws.definition = shownDefinition;
					draws.made = 0;
					for (std::size_t index = 0; index < std::min(looks->extraDraws.size(), ExtraShownLooks::Capacity); ++index)
						if (detail.DrawsModule(looks->extraDraws[index].states.minLodRequired))
							draws.made |= static_cast<std::uint8_t>(1u << index);
				}
				const std::size_t count = std::min(looks->extraDraws.size(), ExtraShownLooks::Capacity);
				for (std::size_t index = 0; index < count; ++index)
				{
					const DefinitionLooks::ExtraDraw &extra = looks->extraDraws[index];
					if (extra.states.Empty() || (draws.made & (1u << index)) == 0)
						continue;
					ShownLook &extraShown = draws.draws[index];
					const ModelStateRolls drawRolls{Key(entity) ^ (static_cast<std::uint64_t>(index + 1) * 0x9E3779B97F4A7C15ull), frame.frame};
					if (fresh)
					{
						const auto initial = static_cast<std::uint32_t>(content::SelectModelState(extra.states, engine::gameplay::Appearance{}.flags));
						ShowModelState(extraShown, extra, clips, initial, frame.clock, drawRolls, false);
						extraShown.state = initial;
					}
					StepModelState(extraShown, extra, clips, static_cast<std::uint32_t>(content::SelectModelState(extra.states, appearance.flags)), frame.clock,
						drawRolls);
					if (paused && extraShown.held < 0.0)
						extraShown.held = frame.clock - extraShown.since;
					if (extraShown.held >= 0.0)
					{
						extraShown.since = frame.clock - extraShown.held;
						if (!paused)
							extraShown.held = -1.0;
					}
					if (hiddenByStealth || extraShown.look >= catalog.lookModels.size() || catalog.lookModels[extraShown.look].empty())
						continue;
					ObjectInstance other = instance;
					other.look = extraShown.look;
					other.clipLook = extraShown.look;
					// AttachToBoneInAnotherModule: at that bone of its own model as drawn now (not found: where it is).
					if (!extra.states.attachToBone.empty() && bones.transform)
						if (const auto bone = bones.transform(instance.look, instance.animationSeconds, instance.animationStart, extra.states.attachToBone))
							other.world = AttachedDrawWorld(instance.world, *bone);
					other.animationSeconds = static_cast<float>((frame.clock - extraShown.since) * extraShown.speed);
					other.animationStart = extraShown.start;
					const bool sunk = extraShown.shown < extra.states.states.size() && extra.states.states[extraShown.shown].adjustHeightByConstruction;
					other.world[11] += (sunk ? constructionSink : 0.0f) - ownSink;
					// Each W3DModelDraw turns its own turret bones by the object's turret angles and handles its own
					// barrels' recoil and muzzle flashes (a Technical's gun is its second draw).
					emit(other);
				}
				if (kept == nullptr)
					context.Commands().Add<ExtraShownLooks>(entity, draws);
			}
			const WeaponPose *aim = weaponPoses.Get(entity);
			presented.push_back({entity, Key(entity), definitions[row].index, look, shownState, at, facing, scale, appearance,
				pose->previous[0] != pose->current[0] || pose->previous[1] != pose->current[1], aim != nullptr && aim->turning != 0, drawn,
				static_cast<float>((frame.clock - since) * speed), start, instance.world});
		}
	}
};
}

// W3DDependencyModelDraw::doDrawModule: a mounted portable structure is drawn only in a frame its carrier's model is
// (hidden, stealthed away, shrouded or gone with it), into the frame's last instance slot.
export namespace generalszh::presentation
{
struct MountedDrawSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Mounted>>;
	using Resources = ecs::Resources<ecs::Read<PresentedObjects>, ecs::Read<MountedInstances>, ecs::Write<ObjectInstances>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		const MountedInstances &mounted = context.Read<MountedInstances>();
		if (mounted.Size() == 0)
			return;
		std::vector<std::uint64_t> drawn;
		context.Read<PresentedObjects>().ForEach([&](const PresentedObject &object) {
			if (object.drawn)
				drawn.push_back(object.key);
		});
		std::sort(drawn.begin(), drawn.end());
		ObjectInstances &instances = context.Write<ObjectInstances>();
		if (instances.SlotCount() == 0)
			return;
		auto &slot = instances.SlotAt(instances.SlotCount() - 1);
		mounted.ForEach([&](const MountedInstance &rider) {
			if (std::binary_search(drawn.begin(), drawn.end(), rider.carrier))
				slot.push_back(rider.instance);
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::MountedDrawSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.mounted_draw";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<generalszh::presentation::ObjectPresentationSystem>;
};

template<>
struct SystemTraits<generalszh::presentation::PoseSampleSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.pose_sample";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::ObjectPresentationSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.objects";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// After this frame's treads and tires have rolled.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<generalszh::presentation::TreadRollSystem, generalszh::presentation::WheelRollSystem>;
};
}
