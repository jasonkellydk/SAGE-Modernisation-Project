export module games.generalszh.presentation.objects.systems.uplink_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.spatial.components.object_shroud;
export import engine.gameplay.rts.construction.components.under_construction;
export import engine.gameplay.rts.construction.components.sale;
export import engine.gameplay.rts.death.components.dying;
export import games.generalszh.gameplay.powers.components.particle_cannon;
export import games.generalszh.presentation.objects.components.uplink_effects;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.resources.object_frame;
export import games.generalszh.presentation.objects.algorithms.laser_beams;
export import games.generalszh.presentation.objects.algorithms.draw_bones;
import Engine.Core.Math.FixedPresentation;

// The Particle Cannon uplink's client effects (ParticleUplinkCannonUpdate):
//   UplinkStatusSystem, once a tick after the simulation, follows the tick's changes in the order they happened:
//   the orbital beam born (createOrbitToTargetLaser: its ground annihilation loop afresh) or gone (the loop stops);
//   each status change (setLogicalStatus's sound loops: idle stops all four; charging starts the powering up loop and
//   stops the others; preparing starts the unpack loop and stops firing's and the annihilation's; ready stops the
//   powering up, firing and annihilation loops; firing starts the firing loop and stops the others; then
//   setClientStatus with no reveal). Then, for each uplink neither sold, unbuilt nor dead: while its viewer cannot see
//   it clearly its effects are removed; seen again, setClientStatus afresh as a reveal. The annihilation loop sounds
//   500 over the beam's spot.
//   UplinkEffectSystem, each drawn frame (after the weapons' lasers): a new client status removes every effect and
//   makes its own (setClientStatus): charging the outer nodes' light flares; preparing their medium flares; almost
//   ready the medium flares, medium connector lasers from each node to the connector bone and the connector's medium
//   flare; ready those and the laser base's light flare; firing the ground-to-orbit laser (from the fire bone 3500 up,
//   widening over WidthGrowTime, at once on a reveal) and every intense effect; after the beam every medium effect and
//   a ground-to-orbit laser narrowing over WidthGrowTime (whole on a reveal). The outer nodes' places are taken the
//   first time effects are made, the connector and fire bones' the first time connector lasers are (the model's bones
//   at rest in the look it shows then: the original reads the drawable's current bone transforms; the flares turn with
//   their bones' yaw only). The orbital beam, while born, runs from 3500 over the beam's spot down to it, as wide as
//   the logic frame after the tick makes it (the drawn frame's LaserRadiusUpdate).
export namespace generalszh::presentation
{
namespace uplink_detail
{
using gameplay::CannonStatus;

inline void SetClient(UplinkEffects &fx, CannonStatus status, bool reveal) noexcept
{
	fx.client = static_cast<std::uint8_t>(status);
	fx.reveal = reveal ? 1 : 0;
	fx.hidden = 0;
	fx.clientAge = 0;
	++fx.clientSerial;
}

inline void Start(UplinkEffects &fx, content::UplinkSound sound) noexcept
{
	fx.want[static_cast<std::size_t>(sound)] = 1;
	++fx.started[static_cast<std::size_t>(sound)];
}

inline void Stop(UplinkEffects &fx, content::UplinkSound sound) noexcept { fx.want[static_cast<std::size_t>(sound)] = 0; }

// setLogicalStatus's sound loops.
inline void EnterStatus(UplinkEffects &fx, CannonStatus status) noexcept
{
	using enum content::UplinkSound;
	switch (status)
	{
	case CannonStatus::Idle:
		Stop(fx, PoweringUp), Stop(fx, UnpackToIdle), Stop(fx, FiringToPack), Stop(fx, GroundAnnihilation);
		break;
	case CannonStatus::Charging:
		Start(fx, PoweringUp), Stop(fx, UnpackToIdle), Stop(fx, FiringToPack), Stop(fx, GroundAnnihilation);
		break;
	case CannonStatus::Preparing:
		Start(fx, UnpackToIdle), Stop(fx, FiringToPack), Stop(fx, GroundAnnihilation);
		break;
	case CannonStatus::ReadyToFire:
		Stop(fx, PoweringUp), Stop(fx, FiringToPack), Stop(fx, GroundAnnihilation);
		break;
	case CannonStatus::Firing:
		Start(fx, FiringToPack), Stop(fx, PoweringUp), Stop(fx, UnpackToIdle), Stop(fx, GroundAnnihilation);
		break;
	default:
		break;
	}
	fx.logical = static_cast<std::uint8_t>(status);
	SetClient(fx, status, false);
}

// The intensities setClientStatus makes for a status (none: std::nullopt): the outer flares, the connector lasers and
// flare, the laser base flare; and whether the ground-to-orbit laser shows.
struct ClientLook
{
	std::optional<content::UplinkIntensity> outer, connector, base;
	bool groundLaser{false};
};

inline ClientLook LookOf(CannonStatus status) noexcept
{
	using enum content::UplinkIntensity;
	switch (status)
	{
	case CannonStatus::Charging:
		return {Light, std::nullopt, std::nullopt, false};
	case CannonStatus::Preparing:
		return {Medium, std::nullopt, std::nullopt, false};
	case CannonStatus::AlmostReady:
		return {Medium, Medium, std::nullopt, false};
	case CannonStatus::ReadyToFire:
		return {Medium, Medium, Light, false};
	case CannonStatus::Firing:
		return {Intense, Intense, Intense, true};
	case CannonStatus::PostFire:
		return {Medium, Medium, Medium, true};
	default:
		return {};
	}
}

inline std::array<float, 3> Up(const std::array<float, 3> &at, float height) noexcept { return {at[0], at[1], at[2] + height}; }

inline constexpr float OrbitalBeamHeight = 3500.0f; // ORBITAL_BEAM_Z_OFFSET
inline constexpr float OrbitalAudioHeight = 500.0f; // ORBITAL_BEAM_AUDIO_Z_OFFSET
}

struct UplinkStatusSystem
{
	using Query = ecs::Query<ecs::Read<gameplay::ParticleCannon>, ecs::Optional<engine::gameplay::ObjectShroud>, ecs::Optional<engine::gameplay::Sale>,
		ecs::Optional<engine::gameplay::UnderConstruction>, ecs::Optional<engine::gameplay::Dying>>;
	using SideTables = ecs::SideTables<ecs::Write<UplinkEffects>>;
	using Resources = ecs::Resources<ecs::Read<gameplay::ParticleCannonEvents>, ecs::Read<PresentationFrame>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		using namespace uplink_detail;
		auto &table = context.Side<SideTables, UplinkEffects>();
		const std::uint32_t viewer = context.Read<PresentationFrame>().viewer;
		for (std::size_t index = 0; index < table.Size(); ++index)
			++table.Value(index).clientAge;
		std::vector<std::pair<ecs::Entity, UplinkEffects>> fresh; // uplinks first followed this tick
		const auto effectsOf = [&](ecs::Entity cannon) -> UplinkEffects & {
			if (UplinkEffects *known = table.Get(cannon))
				return *known;
			for (auto &[entity, effects] : fresh)
				if (entity == cannon)
					return effects;
			return fresh.emplace_back(cannon, UplinkEffects{}).second;
		};
		using Change = gameplay::ParticleCannonEvents::Change;
		for (const Change &change : context.Read<gameplay::ParticleCannonEvents>().changes)
		{
			UplinkEffects &fx = effectsOf(change.cannon);
			switch (change.kind)
			{
			case Change::Kind::BeamBorn:
				Start(fx, content::UplinkSound::GroundAnnihilation);
				fx.beam = 1;
				break;
			case Change::Kind::BeamGone:
				Stop(fx, content::UplinkSound::GroundAnnihilation);
				fx.beam = 0;
				break;
			case Change::Kind::Status:
				EnterStatus(fx, change.status);
				break;
			}
		}
		query.ForEachChunk([&](auto chunk) {
			const auto cannons = chunk.template Get<gameplay::ParticleCannon>();
			const auto shrouds = chunk.template Get<engine::gameplay::ObjectShroud>();
			const bool idle = !chunk.template Get<engine::gameplay::Sale>().empty() || !chunk.template Get<engine::gameplay::UnderConstruction>().empty() ||
				!chunk.template Get<engine::gameplay::Dying>().empty();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < cannons.size(); ++row)
			{
				UplinkEffects &fx = effectsOf(entities[row]);
				if (fx.beam != 0)
				{
					const auto &spot = cannons[row].currentTarget;
					fx.annihilationAt = {Engine::Math::ToFloat(spot.x), Engine::Math::ToFloat(spot.y), Engine::Math::ToFloat(spot.z) + OrbitalAudioHeight};
				}
				if (idle)
					continue;
				// The client player's view (getObservedOrLocalPlayer): anything short of clear hides the effects.
				const bool shrouded = viewer < 64 && !shrouds.empty() && !shrouds[row].ClearTo(viewer);
				if (shrouded)
					fx.hidden = 1;
				else if (fx.shroudedLast != 0)
					SetClient(fx, static_cast<CannonStatus>(fx.logical), true);
				fx.shroudedLast = shrouded ? 1 : 0;
			}
		});
		for (auto &[entity, effects] : fresh)
			context.Commands().Add<UplinkEffects>(entity, std::move(effects));
	}
};

struct UplinkEffectSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Lookup = ecs::Lookup<ecs::Read<gameplay::ParticleCannon>, ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Write<UplinkEffects>, ecs::Read<ExtraShownLooks>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Read<PresentedObjects>, ecs::Read<LookCatalog>, ecs::Read<BonePoses>,
		ecs::Read<TerrainHeightHandle>, ecs::Write<LaserFrame>, ecs::Write<ParticleWorldHandle>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		using namespace uplink_detail;
		auto &table = context.Side<SideTables, UplinkEffects>();
		if (table.Size() == 0)
			return;
		const PresentationFrame &frame = context.Read<PresentationFrame>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const BonePoses &poses = context.Read<BonePoses>();
		const auto &ground = context.Read<TerrainHeightHandle>().at;
		LaserFrame &out = context.Write<LaserFrame>();
		ParticleWorldHandle &particles = context.Write<ParticleWorldHandle>();
		const auto lookup = context.Lookup<Lookup>();
		std::vector<std::pair<ecs::Entity, const PresentedObject *>> shown;
		context.Read<PresentedObjects>().ForEach([&](const PresentedObject &object) {
			if (table.Get(object.entity) != nullptr)
				shown.emplace_back(object.entity, &object);
		});
		const auto create = [&](std::string_view name, const std::array<float, 3> &at, float yaw) -> std::uint64_t {
			if (particles.world == nullptr || particles.content == nullptr || name.empty())
				return 0;
			const auto *definition = particles.content->particles.Find(name);
			return definition != nullptr ? particles.world->Create(*definition, engine::effects::EmitterTransform::At(at[0], at[1], at[2], yaw)) : 0;
		};
		const auto removeAll = [&](UplinkEffects &fx) {
			if (particles.world != nullptr)
				for (const std::uint64_t system : fx.systems)
					particles.world->Destroy(system);
			fx.systems.clear();
			fx.built = 0;
		};
		for (std::size_t index = 0; index < table.Size(); ++index)
		{
			UplinkEffects &fx = table.Value(index);
			const ecs::Entity entity = table.Entities()[index];
			const auto *cannon = lookup.IsAlive(entity) ? lookup.Get<gameplay::ParticleCannon>(entity) : nullptr;
			const auto *definition = lookup.IsAlive(entity) ? lookup.Get<engine::gameplay::DefinitionRef>(entity) : nullptr;
			const DefinitionLooks *looks = definition != nullptr ? catalog.Of(definition->index) : nullptr;
			if (cannon == nullptr || looks == nullptr || !looks->uplink)
				continue;
			const content::UplinkLook &look = *looks->uplink;
			const PresentedObject *object = nullptr;
			for (const auto &[known, found] : shown)
				if (known == entity)
					object = found;
			fx.seenFrame = frame.frame;
			const auto status = static_cast<CannonStatus>(fx.client);
			const ClientLook wanted = LookOf(status);
			if (fx.hidden != 0)
			{
				removeAll(fx);
				fx.builtSerial = fx.clientSerial;
			}
			else if (fx.builtSerial != fx.clientSerial && object != nullptr && object->look < catalog.lookModels.size())
			{
				removeAll(fx);
				const DrawBoneSearch bones{poses, catalog, *looks, *object, context.SideRead<SideTables, ExtraShownLooks>().Get(entity), frame.clock};
				if (Build(fx, look, wanted, *object, bones, create))
				{
					fx.builtSerial = fx.clientSerial;
					fx.built = 1;
					fx.age = 0.0f;
				}
			}
			if (fx.built != 0 && fx.outerCached == 1)
			{
				if (wanted.connector)
					if (const auto &laser = look.connectorLasers[static_cast<std::size_t>(*wanted.connector)])
						for (const auto &node : fx.outer)
							AppendLaserBeams(out, *laser, node, fx.connector, 1.0f, fx.age, ground);
				if (wanted.groundLaser && look.beam)
				{
					// initLaser's growth (initRadius) or setDecayFrames: from the logic frame of the client status on.
					const std::uint64_t ticks = look.widthGrowTicks;
					const float part = ticks == 0 ? 1.0f : static_cast<float>(fx.clientAge + 1) / static_cast<float>(ticks);
					const float scale = fx.reveal != 0 || ticks == 0 ? 1.0f : status == CannonStatus::Firing ? std::min(1.0f, part) : std::max(0.0f, 1.0f - part);
					AppendLaserBeams(out, *look.beam, fx.origin, Up(fx.origin, OrbitalBeamHeight), scale, fx.age, ground);
				}
				fx.age += frame.seconds;
			}
			// The orbital beam, from its birth until it is gone (its own drawable, the logic's).
			if (fx.beam != 0 && look.beam)
			{
				const std::array<float, 3> spot{Engine::Math::ToFloat(cannon->currentTarget.x), Engine::Math::ToFloat(cannon->currentTarget.y),
					Engine::Math::ToFloat(cannon->currentTarget.z)};
				const std::array<float, 3> orbit = Up(spot, OrbitalBeamHeight);
				const float scale = LaserWidthScale(cannon->widening != 0, cannon->widenStart, cannon->widenFinish, cannon->decaying != 0, cannon->decayStart,
					cannon->decayFinish, frame.tick + 1);
				if (fx.orbitSystems[0] == 0)
					fx.orbitSystems[0] = create(look.beam->muzzleSystem, orbit, 0.0f);
				if (fx.orbitSystems[1] == 0)
					fx.orbitSystems[1] = create(look.beam->targetSystem, spot, 0.0f);
				if (particles.world != nullptr)
				{
					if (fx.orbitSystems[0] != 0)
						particles.world->Move(fx.orbitSystems[0], engine::effects::EmitterTransform::At(orbit[0], orbit[1], orbit[2], 0.0f));
					if (fx.orbitSystems[1] != 0)
						particles.world->Move(fx.orbitSystems[1], engine::effects::EmitterTransform::At(spot[0], spot[1], spot[2], 0.0f));
				}
				AppendLaserBeams(out, *look.beam, orbit, spot, scale, fx.orbitAge, ground);
				fx.orbitAge += frame.seconds;
			}
			else
			{
				for (std::uint64_t &system : fx.orbitSystems)
				{
					if (system != 0 && particles.world != nullptr)
						particles.world->Destroy(system);
					system = 0;
				}
				fx.orbitAge = 0.0f;
			}
		}
	}

private:
	// Makes the effects for the client status (false while the model it needs is still loading: none made).
	template<typename Create>
	static bool Build(UplinkEffects &fx, const content::UplinkLook &look, const uplink_detail::ClientLook &wanted, const PresentedObject &object,
		const DrawBoneSearch &bones, const Create &create)
	{
		const BonePoses &poses = bones.poses;
		const float c = std::cos(object.facing), s = std::sin(object.facing);
		const auto world = [&](const std::array<float, 3> &local) {
			const float x = local[0] * object.scale, y = local[1] * object.scale, z = local[2] * object.scale;
			return std::array<float, 3>{object.position[0] + c * x - s * y, object.position[1] + s * x + c * y, object.position[2] + z};
		};
		// calculateDefaultInformation: every outer node bone, or no effects at all.
		if (fx.outerCached == 0)
		{
			if (!poses.pose)
				return false;
			std::vector<std::array<float, 3>> outer;
			std::vector<float> yaws;
			bool found = true;
			for (std::uint32_t node = 0; node < look.outerBones && found; ++node)
			{
				// getMultiLogicalBonePosition: the pristine bones of any of its draw modules.
				const BoneLookup bone = FindDrawBone(bones, std::format("{}{:02}", look.outerBone, node + 1), false);
				if (!bone.ready)
					return false;
				found = bone.found;
				outer.push_back(world(bone.position));
				yaws.push_back(object.facing + bone.yaw);
			}
			fx.outerCached = found ? 1 : UplinkEffects::Invalid;
			fx.outer = std::move(outer);
			fx.outerYaw = std::move(yaws);
		}
		if (fx.outerCached != 1)
			return true;
		// calculateUpBonePositions, as the first connector lasers are made: the connector bone and (when there is a
		// connector bone, as the original checks) the fire bone.
		if (fx.upCached == 0 && (wanted.connector || wanted.groundLaser))
		{
			if (!poses.pose)
				return false;
			if (!look.connectorBone.empty())
			{
				// getCurrentClientBoneTransforms: as drawn now in any of its draw modules (the raised dish's FXConnector, FXMain).
				const BoneLookup connector = FindDrawBone(bones, look.connectorBone, true);
				const BoneLookup fire = FindDrawBone(bones, look.fireBone, true);
				if (!connector.ready || !fire.ready)
					return false;
				if (connector.found)
					fx.connector = world(connector.position);
				if (fire.found)
					fx.origin = world(fire.position);
			}
			fx.upCached = 1;
		}
		const auto keep = [&](std::uint64_t system) {
			if (system != 0)
				fx.systems.push_back(system);
		};
		if (wanted.groundLaser && look.beam)
		{
			keep(create(look.beam->muzzleSystem, fx.origin, 0.0f));
			keep(create(look.beam->targetSystem, uplink_detail::Up(fx.origin, uplink_detail::OrbitalBeamHeight), 0.0f));
		}
		if (wanted.outer)
			for (std::size_t node = 0; node < fx.outer.size(); ++node)
				keep(create(look.outerFlares[static_cast<std::size_t>(*wanted.outer)], fx.outer[node], fx.outerYaw[node]));
		if (wanted.connector)
		{
			if (const auto &laser = look.connectorLasers[static_cast<std::size_t>(*wanted.connector)])
				for (const auto &node : fx.outer)
				{
					keep(create(laser->muzzleSystem, node, 0.0f));
					keep(create(laser->targetSystem, fx.connector, 0.0f));
				}
			keep(create(look.connectorFlares[static_cast<std::size_t>(*wanted.connector)], fx.connector, 0.0f));
		}
		if (wanted.base)
			keep(create(look.baseFlares[static_cast<std::size_t>(*wanted.base)], fx.origin, 0.0f));
		return true;
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::UplinkStatusSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.uplink_status";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};

template<>
struct SystemTraits<generalszh::presentation::UplinkEffectSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.uplink_effects";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
