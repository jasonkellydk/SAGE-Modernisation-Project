export module games.generalszh.presentation.objects.algorithms.tick_effects;
import games.generalszh.gameplay.effects.resources.effect_cues;
import std;
import games.generalszh.gameplay.combat.algorithms.battle_bus;
import games.generalszh.gameplay.powers.components.launcher_door;
import games.generalszh.gameplay.powers.components.particle_cannon;
import engine.gameplay.rts.combat.components.neutron_flight;
import engine.gameplay.rts.delivery.components.delivery;
import games.generalszh.content.combat.combat_catalog;
import engine.gameplay.rts.combat.resources.assists;
import engine.gameplay.rts.death.resources.blast_waves;
import engine.gameplay.rts.propaganda.resources.propaganda_scans;
import games.generalszh.gameplay.mines.components.minefield_generator;
import games.generalszh.gameplay.abilities.resources.sticky_bomb_cues;
import games.generalszh.gameplay.bridges.resources.bridge_cues;
import games.generalszh.presentation.interaction.resources.interaction_resources;
import engine.gameplay.rts.stealth.components.stealth;
import engine.gameplay.rts.stealth.resources.detections;
import games.generalszh.content.stealth.stealth_content;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.rts.combat.systems.auto_fire_system;

export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.session.session_view;
import Engine.Core.Math.FixedPresentation;

// The tick's effects, asked for between ticks (the FX playback system plays
// them in the next frame): things falling over and bouncing where they
// land, muzzle effects where shots were fired, detonations where they
// landed, and death effects where units fell.
export namespace generalszh::presentation
{
namespace tick_effects_detail
{
inline std::array<float, 3> At(const Engine::Math::FixedVector3 &position)
{
	return {Engine::Math::ToFloat(position.x), Engine::Math::ToFloat(position.y), Engine::Math::ToFloat(position.z)};
}

// Object::isLogicallyVisible to the one watching: what it rides in (getOuterObject) seen; a disguiser always; one
// stealthed and not detected not, by a player in the game who is not its ally.
inline bool LogicallyVisible(const session::SessionView &view, ecs::Entity entity)
{
	namespace gp = engine::gameplay;
	const auto &world = view.World();
	if (!world.IsAlive(entity))
		return true;
	if (const auto *carried = world.Get<gp::OffMap>(entity); carried != nullptr && world.IsAlive(carried->holder))
		entity = carried->holder;
	static const std::size_t disguiser = content::KindOfBit("DISGUISER");
	if (const auto definition = view.DefinitionOf(entity); definition && content::HasKindOf(view.Definition(*definition).kinds, disguiser))
		return true;
	const auto *stealth = world.Get<gp::Stealth>(entity);
	if (stealth == nullptr || !stealth->Hidden())
		return true;
	const auto *local = world.FindResource<LocalPlayer>();
	const auto *relationships = world.FindResource<gp::Relationships>();
	const auto *owner = world.Get<gp::Owner>(entity);
	if (local == nullptr || !local->valid || relationships == nullptr || owner == nullptr)
		return true;
	const auto *member = world.Get<gp::TeamMember>(entity);
	return relationships->Between(gp::Relationships::NoTeam, local->player, member != nullptr ? member->team : gp::Relationships::NoTeam, owner->player) ==
		gp::Relationship::Allies;
}
}

// The weapons' exhausts, for weapons that came into play since the last tick.
void KnowExhausts(WeaponExhausts &exhausts, const session::SessionView &view)
{
	for (std::uint32_t weapon = static_cast<std::uint32_t>(exhausts.byWeapon.size()); weapon < view.WeaponCount(); ++weapon)
	{
		const auto *content = view.WeaponContentOf(weapon);
		exhausts.byWeapon.push_back(content != nullptr ? content->exhausts : std::array<std::string, 4>{});
	}
}

// The weapons' recoils (WeaponRecoil, degrees to radians), for weapons that came into play since the last tick.
void KnowRecoils(WeaponRecoils &recoils, const session::SessionView &view)
{
	for (std::uint32_t weapon = static_cast<std::uint32_t>(recoils.byWeapon.size()); weapon < view.WeaponCount(); ++weapon)
	{
		const auto *content = view.WeaponContentOf(weapon);
		recoils.byWeapon.push_back(content != nullptr ? Engine::Math::ToFloat(content->weaponRecoil) * 3.14159265358979f / 180.0f : 0.0f);
	}
}

// The weapons' looping fire sounds (FireSound with a FireSoundLoopTime), for weapons that came into play since the last tick.
void KnowFireLoops(WeaponFireLoops &loops, const session::SessionView &view)
{
	for (std::uint32_t weapon = static_cast<std::uint32_t>(loops.byWeapon.size()); weapon < view.WeaponCount(); ++weapon)
	{
		const auto *content = view.WeaponContentOf(weapon);
		loops.byWeapon.push_back(content != nullptr && content->simulation.fireSoundLoopTicks != 0 ? content->fireSound : std::string{});
	}
}

// The weapons' lasers (LaserName's W3DLaserDraw), for weapons that came into play since the last tick.
void KnowLasers(WeaponLasers &lasers, const session::SessionView &view)
{
	for (std::uint32_t weapon = static_cast<std::uint32_t>(lasers.byWeapon.size()); weapon < view.WeaponCount(); ++weapon)
	{
		WeaponLaser laser;
		if (const auto *content = view.WeaponContentOf(weapon); content != nullptr && !content->laser.empty())
			if (const auto *object = view.ObjectNamed(content->laser))
				if (auto look = content::ReadLaserLook(*object))
				{
					laser.valid = true;
					laser.look = std::move(*look);
					laser.bone = content->laserBone;
				}
		lasers.byWeapon.push_back(std::move(laser));
	}
}

// The weapons' projectile streams (ProjectileStreamName's W3DProjectileStreamDraw), for weapons that came into play since the
// last tick.
inline void KnowStreams(WeaponStreams &streams, const session::SessionView &view)
{
	for (std::uint32_t weapon = static_cast<std::uint32_t>(streams.byWeapon.size()); weapon < view.WeaponCount(); ++weapon)
	{
		std::optional<content::StreamLook> look;
		if (const auto *content = view.WeaponContentOf(weapon); content != nullptr && !content->projectileStream.empty())
			if (const auto *object = view.ObjectNamed(content->projectileStream))
				look = content::ReadStreamLook(*object);
		streams.byWeapon.push_back(std::move(look));
	}
}

// A laser object's look by name (AssistedTargetingUpdate's LaserFromAssisted / LaserToTarget), known from then on.
inline std::optional<std::uint32_t> LaserObject(WeaponLasers &lasers, const session::SessionView &view, std::string_view name)
{
	if (name.empty())
		return std::nullopt;
	for (std::uint32_t index = 0; index < lasers.byObject.size(); ++index)
		if (lasers.byObject[index].first == name)
			return WeaponLasers::ObjectBase + index;
	WeaponLaser laser;
	if (const auto *object = view.ObjectNamed(name))
		if (auto look = content::ReadLaserLook(*object))
		{
			laser.valid = true;
			laser.look = std::move(*look);
		}
	lasers.byObject.emplace_back(std::string(name), std::move(laser));
	return WeaponLasers::ObjectBase + static_cast<std::uint32_t>(lasers.byObject.size() - 1);
}

// The tick's assists (AssistedTargetingUpdate::assistAttack): a data stream from whoever asked to the helper, and one
// from the helper to its target (makeFeedbackLaser: a laser object from one to the other).
void QueueAssistLasers(LaserRequests &requests, WeaponLasers &lasers, session::SessionView &view)
{
	const auto *assists = view.World().FindResource<engine::gameplay::Assists>();
	if (assists == nullptr)
		return;
	for (const engine::gameplay::Assist &assist : assists->list)
	{
		const auto definition = view.DefinitionOf(assist.assister);
		if (!definition)
			continue;
		const auto look = content::ReadAssistedTargeting(view.Definition(*definition));
		if (!look)
			continue;
		if (const auto laser = LaserObject(lasers, view, look->laserFromAssisted))
			requests.pending.push_back({*laser, assist.requester, assist.assister, {}});
		if (const auto laser = LaserObject(lasers, view, look->laserToTarget))
			requests.pending.push_back({*laser, assist.assister, assist.victim, {}});
	}
}

// The tick's laser shots (a weapon with a LaserName: its own and point defense lasers), for the laser system.
void QueueTickLasers(LaserRequests &requests, const WeaponLasers &lasers, const session::SessionView &view)
{
	using namespace tick_effects_detail;
	const auto queue = [&](const engine::gameplay::Shot &shot) {
		if (lasers.Of(shot.weapon) != nullptr)
			requests.pending.push_back({shot.weapon, shot.source, shot.target, At(shot.aim)});
	};
	view.Fired().ForEach(queue);
	view.DefenseShots().ForEach(queue);
}

void QueueTickEffects(FxRequests &fx, const LookCatalog &catalog, const session::SessionView &view)
{
	using namespace tick_effects_detail;
	for (const auto &event : view.ToppleEvents())
		if (const auto definition = view.DefinitionOf(event.entity))
			if (const DefinitionLooks *looks = catalog.Of(*definition))
			{
				const std::string &name = event.kind == engine::gameplay::ToppleEvent::Kind::Started ? looks->toppleFX : looks->bounceFX;
				if (!name.empty())
					fx.pending.push_back({name, At(event.position), 0.0f, 0.0f, event.entity});
			}
	// Weapon::fireWeaponTemplate's FXList::doFXPos (the primary damage radius as its caller's) for what fired this tick:
	// the weapons', and what fired itself (FireWeaponUpdate's forceFireWeapon), unless its FX are still suspended.
	const auto fired = [&](const engine::gameplay::Shot &shot) {
		const auto *weapon = view.WeaponContentOf(shot.weapon);
		if (weapon == nullptr || weapon->FireFX(shot.veterancy).empty() || shot.quiet != 0)
			return;
		// Weapon::fireWeaponTemplate: a firer the one watching cannot see shows no fire FX, unless it is a mine or its
		// weapon says to (PlayFXWhenStealthed).
		static const std::size_t mine = content::KindOfBit("MINE");
		const auto definition = view.DefinitionOf(shot.source);
		if (!weapon->playFXWhenStealthed && !(definition && content::HasKindOf(view.Definition(*definition).kinds, mine)) &&
			!LogicallyVisible(view, shot.source))
			return;
		const auto from = At(shot.origin), to = At(shot.aim);
		FxRequest request{weapon->FireFX(shot.veterancy), from, std::atan2(to[1] - from[1], to[0] - from[0]),
			Engine::Math::ToFloat(weapon->simulation.primaryRadius * shot.radiusScale)};
		request.firedBy = shot.source; // presentation moves it onto the barrel
		request.firedSlot = shot.slot;
		request.hasSecondary = true;
		request.secondary = to;
		request.speed = Engine::Math::ToFloat(weapon->simulation.speed);
		fx.pending.push_back(std::move(request));
	};
	view.Fired().ForEach(fired);
	if (const auto *autoShots = view.World().FindResource<engine::gameplay::AutoShots>())
		autoShots->ForEach(fired);
	for (const auto &impact : view.Impacts())
		if (const auto *weapon = view.WeaponContentOf(impact.weapon); weapon != nullptr && !weapon->DetonationFX(impact.veterancy).empty())
			fx.pending.push_back({weapon->DetonationFX(impact.veterancy), At(impact.position), 0.0f, 0.0f});
	// NeutronMissileSlowDeathBehavior::doBlast: the scorch mark its first hurting blast leaves (addScorch, SCORCH_1).
	if (const auto *waves = view.World().FindResource<engine::gameplay::BlastWaves>())
		for (const engine::gameplay::BlastScorchMark &mark : waves->marks)
		{
			FxRequest scorch{{}, At(mark.position), 0.0f, 0.0f};
			scorch.scorchRadius = Engine::Math::ToFloat(mark.size);
			fx.pending.push_back(std::move(scorch));
		}
	// GenerateMinefieldBehavior::placeMines: its GenerationFX where it laid them.
	if (const auto *mines = view.World().FindResource<generalszh::gameplay::MinefieldEffects>())
		for (const auto &played : mines->played)
			fx.pending.push_back({played.effect,
				{static_cast<float>(played.at[0]) / 65536.0f, static_cast<float>(played.at[1]) / 65536.0f, static_cast<float>(played.at[2]) / 65536.0f}, 0.0f, 0.0f});
	// StickyBombUpdate::detonate: a booby trap's GeometryBasedDamageFX where its victim stands, over the blast's reach
	// (doFXPos with its secondary radius).
	if (const auto *bombs = view.World().FindResource<generalszh::gameplay::StickyBombCues>())
		for (const auto &cue : bombs->list)
			if (cue.kind == generalszh::gameplay::StickyBombCue::Kind::Effect)
				fx.pending.push_back({std::string(view.DeathEffectName(engine::gameplay::DeathEffectKind::Effect, cue.effect)), At(cue.at), 0.0f,
					Engine::Math::ToFloat(cue.radius)});
	// The game logic's own FX lists (EffectCues: doFXObj on an object, doFXPos at a spot).
	if (const auto *cues = view.World().FindResource<generalszh::gameplay::EffectCues>())
		for (const auto &cue : cues->list)
		{
			FxRequest request{cue.particleSystem ? std::string{} : cue.effect, At(cue.at), 0.0f, 0.0f, cue.on};
			if (cue.particleSystem)
				request.particleSystem = cue.effect;
			request.scorchRadius = Engine::Math::ToFloat(cue.scorch);
			fx.pending.push_back(std::move(request));
		}
	// DeliverPayloadAIUpdate::update: a diving carrier's StrafeWeaponFX at each strafe point (doFXPos).
	if (const auto *runs = view.World().FindResource<engine::gameplay::DeliveryCues>())
		runs->ForEach([&](const engine::gameplay::DeliveryCue &cue) {
			if (cue.kind == engine::gameplay::DeliveryCue::Kind::Strafe && cue.effect != engine::gameplay::Delivery::NoEffect)
				fx.pending.push_back({std::string(view.DeathEffectName(engine::gameplay::DeathEffectKind::Effect, cue.effect)), At(cue.at), 0.0f, 0.0f});
		});
	// BattleBusSlowDeathBehavior: FXStartUndeath and FXHitGround on the bus (doFXObj).
	if (const auto *buses = view.World().FindResource<generalszh::gameplay::BattleBusCues>())
		for (const auto &cue : buses->list)
			fx.pending.push_back({cue.effect, At(cue.at), 0.0f, 0.0f, cue.bus});
	// BridgeBehavior: a transition's area effects and its BridgeDieFX (doFXPos).
	if (const auto *bridges = view.World().FindResource<generalszh::gameplay::BridgeCues>())
		for (const auto &cue : bridges->list)
			if (!cue.sound)
				fx.pending.push_back({cue.name, At(cue.at), 0.0f, 0.0f});
	// NeutronMissileUpdate::doLaunch: LaunchFX and IgnitionFX on the missile (doFXObj).
	if (const auto *missiles = view.World().FindResource<engine::gameplay::NeutronEffects>())
		for (const auto &played : missiles->played)
			fx.pending.push_back({std::string(view.DeathEffectName(engine::gameplay::DeathEffectKind::Effect, played.effect)), At(played.at), 0.0f, 0.0f, played.missile});
	// ParticleUplinkCannonUpdate: the beam's scorch marks (addScorch) and its GroundHitFX / BeamLaunchFX (doFXPos).
	if (const auto *cannons = view.World().FindResource<generalszh::gameplay::ParticleCannonEvents>())
	{
		for (const auto &scorch : cannons->scorches)
		{
			FxRequest mark{{}, At(scorch.at), 0.0f, 0.0f};
			mark.scorchRadius = Engine::Math::ToFloat(scorch.radius);
			fx.pending.push_back(std::move(mark));
		}
		for (const auto &played : cannons->played)
			fx.pending.push_back({std::string(view.DeathEffectName(engine::gameplay::DeathEffectKind::Effect, played.effect)), At(played.at), 0.0f, 0.0f});
	}
	// MissileLauncherBuildingUpdate::switchToState: each door state's effect where the superweapon stands (doFXPos).
	if (const auto *doors = view.World().FindResource<generalszh::gameplay::LauncherDoorEffects>())
		for (const auto &played : doors->played)
			fx.pending.push_back({std::string(view.DeathEffectName(engine::gameplay::DeathEffectKind::Effect, played.effect)), At(played.at), 0.0f, 0.0f});
	// PropagandaTowerBehavior::doScan: a tower's pulse (upgraded or not) on it as it scans.
	if (const auto *scans = view.World().FindResource<engine::gameplay::PropagandaScans>())
		for (const engine::gameplay::PropagandaPulse &pulse : scans->pulses)
			fx.pending.push_back({std::string(view.DeathEffectName(engine::gameplay::DeathEffectKind::Effect, pulse.effect)), At(pulse.position), 0.0f, 0.0f, pulse.tower});
	// StealthUpdate::changeVisualDisguise: a disguiser's DisguiseFX where it stands as it takes the look, its
	// DisguiseRevealFX as it loses it (doFXPos).
	if (const auto *disguises = view.World().FindResource<engine::gameplay::DisguiseEvents>())
		disguises->ForEach([&](const engine::gameplay::DisguiseEvent &event) {
			const auto definition = view.DefinitionOf(event.entity);
			if (!definition)
				return;
			if (const auto stealth = content::ReadObjectStealth(view.Definition(*definition), engine::time::FixedStep{30}))
				if (const std::string &name = event.disguised != 0 ? stealth->disguiseFx : stealth->disguiseRevealFx; !name.empty())
					fx.pending.push_back({name, At(event.position), 0.0f, 0.0f});
		});
	for (const auto &event : view.DeathEvents())
		if (event.kind == engine::gameplay::DeathEffectKind::Effect)
			// FXListDie: on the object facing its way (doFXObj), or unrotated where it was (OrientToObject No: doFXPos).
			fx.pending.push_back({std::string(view.DeathEffectName(event.kind, event.id)), At(event.position),
				event.orient ? static_cast<float>(event.facing.units) * 6.283185307179586f / 4294967296.0f : 0.0f, 0.0f,
				event.orient ? event.entity : ecs::Entity{}});
}
}
