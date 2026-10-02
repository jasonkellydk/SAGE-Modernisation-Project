export module games.generalszh.presentation.objects.systems.effect_attachment_systems;
import engine.gameplay.common.health.components.health;
import std;
import engine.gameplay.common.appearance.components.debris_look;

export import engine.ecs.system.system;
export import engine.gameplay.common.identity.components.definition_ref;
export import games.generalszh.presentation.objects.components.effect_attachments;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.resources.object_frame;
export import games.generalszh.presentation.objects.resources.dynamic_lights;
export import games.generalszh.presentation.objects.systems.object_presentation_systems;
export import games.generalszh.presentation.objects.algorithms.barrel_placement;
export import games.generalszh.presentation.objects.algorithms.laser_beams;
export import engine.gameplay.rts.combat.components.missile;
export import games.generalszh.gameplay.abilities.components.ability_laser;
export import engine.gameplay.rts.death.components.height_die;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.resources.deck_surfaces;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.spatial.components.object_shroud;
import Engine.Core.Math.FixedPresentation;

// Particle systems riding on objects, each frame after the objects are
// presented (one pass: the particle world is the side effect): an object
// entering a model state starts the systems the state carries at their bones
// (oriented like the bone; the original's ParticleSysBone), leaving it
// destroys them (W3DModelDraw::stopClientParticleSystems: they emit no more, what is out lives on); a dying helicopter trails its
// smoke for the rest of its death. Systems follow their objects; objects
// no longer presented lose theirs.
export namespace generalszh::presentation
{
namespace effect_attachment_detail
{
inline engine::effects::EmitterTransform Place(const PresentedObject &object, const AttachedSystem &attached)
{
	const float c = std::cos(object.facing), s = std::sin(object.facing);
	const float x = attached.local[0] * object.scale, y = attached.local[1] * object.scale, z = attached.local[2] * object.scale;
	return engine::effects::EmitterTransform::At(object.position[0] + c * x - s * y, object.position[1] + s * x + c * y, object.position[2] + z,
		object.facing + attached.yaw);
}

inline void Remove(engine::effects::ParticleWorld &particles, std::vector<AttachedSystem> &systems)
{
	for (const AttachedSystem &attached : systems)
		particles.Stop(attached.id);
	systems.clear();
}

// Starts `bones`' systems on `object`; false while its model is still loading (try again next frame).
inline bool Start(std::vector<AttachedSystem> &systems, std::span<const content::ModelState::ParticleBone> bones, const PresentedObject &object,
	std::string_view model, const BonePoses &poses, const ParticleWorldHandle &particles)
{
	std::vector<AttachedSystem> placed(bones.size());
	for (std::size_t index = 0; index < bones.size(); ++index)
	{
		// No bone: at its offset in the object's frame (none: the origin).
		if (bones[index].bone.empty())
			placed[index].local = {Engine::Math::ToFloat(bones[index].offset.x), Engine::Math::ToFloat(bones[index].offset.y),
				Engine::Math::ToFloat(bones[index].offset.z)};
		if (bones[index].bone.empty() || !poses.pose)
			continue;
		// A model that draws nothing has no bones: its systems sit at the origin.
		const BoneLookup pose = model.empty() ? BoneLookup{true, false} : poses.pose(model, bones[index].bone);
		if (!pose.ready)
			return false;
		// A bone the model does not have: the object's origin, as the original.
		if (pose.found)
			placed[index] = {0, pose.position, pose.yaw, static_cast<std::int32_t>(index)};
	}
	for (std::size_t index = 0; index < bones.size(); ++index)
		if (const auto *definition = particles.content->particles.Find(bones[index].system))
		{
			AttachedSystem attached = placed[index];
			attached.id = particles.world->Create(*definition, Place(object, attached));
			// Its draw module switches it off and on (ParticleSystem::stop / start); only destroy() ends it.
			particles.world->Hold(attached.id);
			systems.push_back(attached);
		}
	return true;
}

// ParticleSystem::update's isShrouded for a system attached to an object: the viewer sees the object fogged or shrouded
// (getShroudedStatus >= OBJECTSHROUD_FOGGED: neither clear nor partly clear). No viewer, or no shroud: never.
inline bool ShroudedFromViewer(const engine::gameplay::ObjectShroud *shroud, std::uint32_t viewer) noexcept
{
	return shroud != nullptr && viewer != PresentationFrame::NoViewer && viewer < 64 && !shroud->SeenBy(viewer);
}

// W3DModelDraw::doStartOrStopParticleSys: its systems stop (emit nothing, kept) while the drawable is hidden or fully
// obscured by the shroud, and start again when it is not.
inline void SwitchSystems(engine::effects::ParticleWorld &particles, const std::vector<AttachedSystem> &systems, bool on)
{
	for (const AttachedSystem &attached : systems)
		if (on)
			particles.Resume(attached.id);
		else
			particles.Pause(attached.id);
}

inline void ObscureSystems(engine::effects::ParticleWorld &particles, const std::vector<AttachedSystem> &systems, bool obscured)
{
	for (const AttachedSystem &attached : systems)
		particles.SetObscured(attached.id, obscured);
}

// ParticleSystem::update for a system attached to an object that is not drawn this frame (hidden by the shroud): it
// still follows the object's transform (getTransformMatrix), here the simulation's position and facing at the look's
// scale; and it emits nothing (obscured).
inline void FollowHidden(engine::effects::ParticleWorld &particles, const std::vector<AttachedSystem> &systems, const engine::gameplay::Transform *transform,
	float scale)
{
	if (transform != nullptr)
	{
		PresentedObject at;
		at.position = {Engine::Math::ToFloat(transform->position.x), Engine::Math::ToFloat(transform->position.y), Engine::Math::ToFloat(transform->position.z)};
		at.facing = static_cast<float>(transform->facing.units) * 6.283185307179586f / 4294967296.0f;
		at.scale = scale;
		for (const AttachedSystem &attached : systems)
			particles.Move(attached.id, Place(at, attached));
	}
	ObscureSystems(particles, systems, true);
}

// The look's scale for an object's definition (1 when unknown).
inline float LookScale(const LookCatalog &catalog, const engine::gameplay::DefinitionRef *definition) noexcept
{
	const DefinitionLooks *looks = definition != nullptr ? catalog.Of(definition->index) : nullptr;
	return looks != nullptr ? looks->scale : 1.0f;
}
}

// W3DModelDraw::getCurrentBoneTransforms then Thing::transformBoneToWorld (FXListAtBonePosFXNugget::doFxAtBones): the
// world transforms (row-major 3x4) of `object`'s bones as its model is drawn now: `bone` itself (start 0), or bone01,
// bone02, ... up to bone99 (start 1), stopping at the first its model does not have, at most 40. Each is the bone's
// transform in its model scaled by the object's scale (getCurrentBoneTransforms' model-scaled inverse), then turned
// by the object's facing and moved to where it is. None while its model is loading.
inline std::vector<std::array<float, 12>> CurrentBoneTransforms(const PresentedObject &object, const BonePoses &poses, std::string_view bone, int start)
{
	std::vector<std::array<float, 12>> found;
	if (!poses.transform)
		return found;
	constexpr std::size_t MaxBonePoints = 40;
	const float c = std::cos(object.facing), s = std::sin(object.facing);
	const std::array<float, 9> turn{c, -s, 0, s, c, 0, 0, 0, 1};
	const int last = start == 0 ? 0 : 99;
	for (int index = start; index <= last && found.size() < MaxBonePoints; ++index)
	{
		const std::string name = index == 0 ? std::string(bone) : std::format("{}{:02}", bone, index);
		const auto local = poses.transform(object.look, object.animationSeconds, object.animationStart, name);
		if (!local)
			break;
		std::array<float, 12> world{};
		for (std::size_t row = 0; row < 3; ++row)
			for (std::size_t column = 0; column < 4; ++column)
				world[row * 4 + column] = object.scale * (turn[row * 3] * (*local)[column] + turn[row * 3 + 1] * (*local)[4 + column] +
					turn[row * 3 + 2] * (*local)[8 + column]) + (column == 3 ? object.position[row] : 0.0f);
		found.push_back(world);
	}
	return found;
}

// The frame's weapon fire FX moved onto the barrels that fired them, once the objects are presented.
struct FireFxPlacementSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Read<WeaponPose>>;
	using Resources = ecs::Resources<ecs::Read<PresentedObjects>, ecs::Read<LookCatalog>, ecs::Read<BonePoses>, ecs::Write<FxRequests>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		FxRequests &requests = context.Write<FxRequests>();
		if (std::none_of(requests.pending.begin(), requests.pending.end(), [](const FxRequest &request) { return request.firedBy.IsValid(); }))
			return;
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const BonePoses &poses = context.Read<BonePoses>();
		const auto &weapons = context.SideRead<SideTables, WeaponPose>();
		context.Read<PresentedObjects>().ForEach([&](const PresentedObject &object) {
			const DefinitionLooks *looks = catalog.Of(object.definition);
			if (looks == nullptr || looks->states.Empty() || object.look >= catalog.lookModels.size() || object.state >= looks->states.states.size())
				return;
			const WeaponPose *weapon = weapons.Get(object.entity);
			for (FxRequest &request : requests.pending)
			{
				if (request.firedBy != object.entity)
					continue;
				request.firedBy = {};
				// The slot's FireFX bone; on the second turret when it hangs there.
				const content::ModelState &state = looks->states.states[object.state];
				const std::string_view model = catalog.lookModels[object.look];
				const std::string &slotBone = state.slotFireFxBones[request.firedSlot < 3 ? request.firedSlot : 0];
				const bool alt = !slotBone.empty() && !state.altTurretBone.empty() && poses.descends &&
					(poses.descends(model, slotBone, state.altTurretBone) || poses.descends(model, slotBone + "01", state.altTurretBone));
				const float turret = weapon == nullptr ? 0.0f : alt ? weapon->currentAltTurret : weapon->currentTurret;
				const float pitch = weapon == nullptr ? 0.0f : alt ? weapon->currentAltPitch : weapon->currentPitch;
				if (const auto place = PlaceOnBarrel(object, state, model, poses, turret, pitch, weapon != nullptr ? weapon->barrel : 0u, nullptr,
						slotBone.empty() ? std::string_view(state.fireFxBone) : std::string_view(slotBone), alt))
				{
					request.at = place->at;
					request.yaw = place->yaw;
				}
			}
		});
	}
};

// Each tick, whether each missile's exhaust burns (MissileAIUpdate: lit at ignition, tossed once its
// fuel runs out or it burns itself out).
// A missile's motor lighting (MissileAIUpdate::doIgnitionState: its IgnitionFX), once a tick before the exhausts
// are sampled: lit this tick (lit and new, or lit and not lit the tick before).
struct MissileIgnitionSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::MissileFlight>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Transform>>;
	using SideTables = ecs::SideTables<ecs::Read<ExhaustState>>;
	using Resources = ecs::Resources<ecs::Read<LookCatalog>, ecs::Write<FxRequests>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const auto &states = context.SideRead<SideTables, ExhaustState>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		auto &fx = context.Write<FxRequests>().pending;
		query.ForEachChunk([&](auto chunk) {
			const auto missiles = chunk.template Get<engine::gameplay::MissileFlight>();
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto transforms = chunk.template Get<engine::gameplay::Transform>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < missiles.size(); ++row)
			{
				const ExhaustState *known = states.Get(entities[row]);
				if (!missiles[row].exhaustLit || (known != nullptr && known->lit != 0))
					continue;
				const DefinitionLooks *looks = catalog.Of(definitions[row].index);
				if (looks == nullptr || looks->ignitionFX.empty())
					continue;
				const auto &transform = transforms[row];
				FxRequest request{looks->ignitionFX,
					{Engine::Math::ToFloat(transform.position.x), Engine::Math::ToFloat(transform.position.y), Engine::Math::ToFloat(transform.position.z)},
					static_cast<float>(transform.facing.units) * 6.283185307179586f / 4294967296.0f, 0.0f};
				request.object = entities[row];
				fx.push_back(std::move(request));
			}
		});
	}
};

struct ExhaustSampleSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::MissileFlight>>;
	using SideTables = ecs::SideTables<ecs::Write<ExhaustState>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		auto &states = context.Side<SideTables, ExhaustState>();
		const auto missiles = chunk.Get<engine::gameplay::MissileFlight>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < missiles.size(); ++row)
		{
			const engine::gameplay::MissileFlight &missile = missiles[row];
			const ExhaustState state{missile.shot.weapon, missile.exhaustLit ? 1u : 0u, missile.shot.veterancy};
			if (ExhaustState *known = states.Get(entities[row]))
				*known = state;
			else
				context.Commands().Add<ExhaustState>(entities[row], state);
		}
	}
};

// Each frame, missiles' exhausts (their weapon's ProjectileExhaust) trail them from their origin while
// they burn; tossed (and for a missile gone), the system stops and what is out lives on.
struct ExhaustSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Read<ExhaustState>, ecs::Write<ExhaustEmission>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::ObjectShroud>, ecs::Read<engine::gameplay::Transform>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Read<PresentedObjects>, ecs::Read<WeaponExhausts>, ecs::Write<ParticleWorldHandle>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		ParticleWorldHandle &particles = context.Write<ParticleWorldHandle>();
		if (particles.world == nullptr || particles.content == nullptr)
			return;
		const std::uint32_t serial = context.Read<PresentationFrame>().frame;
		const WeaponExhausts &exhausts = context.Read<WeaponExhausts>();
		const auto &states = context.SideRead<SideTables, ExhaustState>();
		auto &emissions = context.Side<SideTables, ExhaustEmission>();
		auto &commands = context.Commands();
		const auto lookup = context.Lookup<Lookup>();
		const std::uint32_t viewer = context.Read<PresentationFrame>().viewer;
		// MissileAIUpdate's exhaust rides the missile (attachToObject): nothing emitted while the viewer sees it fogged or
		// shrouded (ParticleSystem::update's isShrouded), and kept while it is.
		const auto shrouded = [&](ecs::Entity entity) {
			return lookup.IsAlive(entity) && effect_attachment_detail::ShroudedFromViewer(lookup.Get<engine::gameplay::ObjectShroud>(entity), viewer);
		};
		context.Read<PresentedObjects>().ForEach([&](const PresentedObject &object) {
			const ExhaustState *state = states.Get(object.entity);
			if (state == nullptr)
				return;
			const auto at = engine::effects::EmitterTransform::At(object.position[0], object.position[1], object.position[2], object.facing);
			ExhaustEmission *emission = emissions.Get(object.entity);
			if (emission != nullptr)
				emission->seenFrame = serial;
			if (state->lit == 0)
			{
				if (emission != nullptr && emission->id != 0 && emission->tossed == 0)
				{
					particles.world->Stop(emission->id);
					emission->tossed = 1;
				}
				return;
			}
			if (emission != nullptr && emission->id != 0)
			{
				if (emission->tossed == 0)
				{
					particles.world->Move(emission->id, at);
					particles.world->SetObscured(emission->id, shrouded(object.entity));
				}
				return;
			}
			const std::string_view name = exhausts.Of(state->weapon, state->veterancy);
			const auto *definition = name.empty() ? nullptr : particles.content->particles.Find(name);
			if (definition == nullptr)
				return;
			const std::uint64_t id = particles.world->Create(*definition, at);
			if (emission != nullptr)
				*emission = {id, serial, 0};
			else
				commands.Add<ExhaustEmission>(object.entity, ExhaustEmission{id, serial, 0});
		});
		// Missiles no longer presented (gone): their exhausts stop; hidden by the shroud, they emit nothing meanwhile.
		for (std::size_t index = 0; index < emissions.Size(); ++index)
			if (ExhaustEmission &emission = emissions.Value(index); emission.seenFrame != serial && emission.id != 0 && emission.tossed == 0)
			{
				if (const ecs::Entity entity = emissions.Entities()[index]; shrouded(entity))
				{
					// Following the missile meanwhile (attached: its transform), emitting nothing.
					if (const auto *transform = lookup.Get<engine::gameplay::Transform>(entity))
						particles.world->Move(emission.id, engine::effects::EmitterTransform::At(Engine::Math::ToFloat(transform->position.x),
							Engine::Math::ToFloat(transform->position.y), Engine::Math::ToFloat(transform->position.z),
							static_cast<float>(transform->facing.units) * 6.283185307179586f / 4294967296.0f));
					particles.world->SetObscured(emission.id, true);
					continue;
				}
				particles.world->Stop(emission.id);
				emission.tossed = 1;
			}
	}
};

// Laser beams (a weapon's LaserName: LaserUpdate driving W3DLaserDraw), each frame after the objects are
// presented: a laser shot puts its beam out with its muzzle and target particle systems; the beam runs from its
// shooter's laser bone (turned with the turret) to where it was aimed, for its lifetime
// (LifetimeUpdate), then goes (its systems stop). Its start follows its shooter (updateStartPos); its end stays where
// it was put (the callers all give initLaser an end, so updateEndPos never follows a target). This frame's beams: W3DLaserDraw's NumBeams from the inner to
// the outer width and colour (the original's colour ramp scaled by the inner alpha), over its segments (arched
// by ArcHeight on a cosine, never below 2 over the ground), their texture tiled by length over width. Its muzzle and
// target particle systems are put where it starts and ends as it begins (initLaser), and stay there.
// An ability's laser special object (AbilityLaser: the Missile Defender's, the hackers') is drawn the same way for as
// long as it exists: from its unit's SpecialObjectAttachToBone (its position when it has no such bone), followed, to the
// target's centre as it began; its particle systems (when its unit is seen as it starts) go with it.
struct LaserSystem
{
	using Query = ecs::Query<ecs::Read<generalszh::gameplay::AbilityLaser>, ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Read<WeaponPose>, ecs::Write<AbilityLaserView>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Read<PresentedObjects>, ecs::Read<LookCatalog>, ecs::Read<BonePoses>,
		ecs::Read<WeaponLasers>, ecs::Read<TerrainHeightHandle>, ecs::Write<LaserRequests>, ecs::Write<ActiveLasers>, ecs::Write<LaserFrame>,
		ecs::Write<ParticleWorldHandle>, ecs::Write<PresentationRandom>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const PresentationFrame &frame = context.Read<PresentationFrame>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const BonePoses &poses = context.Read<BonePoses>();
		const WeaponLasers &weapons = context.Read<WeaponLasers>();
		auto &abilityViews = context.Side<SideTables, AbilityLaserView>();
		const GroundHeightAt &ground = context.Read<TerrainHeightHandle>().at;
		auto &requests = context.Write<LaserRequests>().pending;
		auto &active = context.Write<ActiveLasers>().lasers;
		LaserFrame &out = context.Write<LaserFrame>();
		ParticleWorldHandle &particles = context.Write<ParticleWorldHandle>();
		const auto &weaponPoses = context.SideRead<SideTables, WeaponPose>();
		out.beams.clear();
		std::vector<std::pair<ecs::Entity, const generalszh::gameplay::AbilityLaser *>> abilityLasers;
		std::vector<std::uint32_t> abilityDefinitions;
		query.ForEachChunk([&](auto chunk) {
			const auto lasers = chunk.template Get<generalszh::gameplay::AbilityLaser>();
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < lasers.size(); ++row)
			{
				abilityLasers.emplace_back(entities[row], &lasers[row]);
				abilityDefinitions.push_back(definitions[row].index);
			}
		});
		if (active.empty() && requests.empty() && abilityLasers.empty())
			return;
		// The objects the lasers hang on, found in one pass over this frame's objects.
		std::vector<std::pair<ecs::Entity, const PresentedObject *>> shown;
		for (const LaserRequest &request : requests)
			shown.emplace_back(request.source, nullptr), shown.emplace_back(request.target, nullptr);
		for (const ActiveLaser &laser : active)
			shown.emplace_back(laser.source, nullptr);
		for (const auto &[entity, laser] : abilityLasers)
			shown.emplace_back(laser->parent, nullptr);
		context.Read<PresentedObjects>().ForEach([&](const PresentedObject &object) {
			for (auto &[entity, found] : shown)
				if (entity == object.entity)
					found = &object;
		});
		const auto presented = [&](ecs::Entity entity) -> const PresentedObject * {
			if (entity == ecs::Entity{})
				return nullptr;
			for (const auto &[known, found] : shown)
				if (known == entity)
					return found;
			return nullptr;
		};
		// The source's bone (turned with its turret), or its position without one.
		const auto boneOf = [&](ecs::Entity from, std::string_view bone) -> std::optional<std::array<float, 3>> {
			const PresentedObject *source = presented(from);
			if (source == nullptr)
				return std::nullopt;
			const DefinitionLooks *looks = catalog.Of(source->definition);
			if (looks != nullptr && !looks->states.Empty() && source->look < catalog.lookModels.size() && source->state < looks->states.states.size())
			{
				const WeaponPose *aim = weaponPoses.Get(from);
				if (const auto place = PlaceOnBarrel(*source, looks->states.states[source->state], catalog.lookModels[source->look], poses,
						aim != nullptr ? aim->currentTurret : 0.0f, aim != nullptr ? aim->currentPitch : 0.0f, 0, nullptr, bone))
					return place->at;
			}
			return source->position;
		};
		const auto startOf = [&](const ActiveLaser &laser, const WeaponLaser &look) { return boneOf(laser.source, look.bone); };
		const auto systemAt = [&](std::string_view name, const std::array<float, 3> &at) -> std::uint64_t {
			if (particles.world == nullptr || particles.content == nullptr || name.empty())
				return 0;
			const auto *definition = particles.content->particles.Find(name);
			return definition != nullptr ? particles.world->Create(*definition, engine::effects::EmitterTransform::At(at[0], at[1], at[2], 0.0f)) : 0;
		};
		for (const LaserRequest &request : requests)
			if (const WeaponLaser *look = weapons.Of(request.weapon))
			{
				ActiveLaser laser{request.weapon, request.source, request.target, {}, request.end, 0.0f, 0.0f, 0, 0};
				// LifetimeUpdate: a whole number of frames between its least and most.
				const auto frames = std::uniform_int_distribution<std::uint32_t>(look->look.minLifetimeFrames, look->look.maxLifetimeFrames)(
					context.Write<PresentationRandom>().engine);
				laser.lifetime = static_cast<float>(frames) / 30.0f;
				laser.start = startOf(laser, *look).value_or(request.end);
				// AssistedTargetingUpdate's streams end where their target is as they start (to->getPosition()).
				if (request.atTarget)
					if (const PresentedObject *target = presented(request.target))
						laser.end = target->position;
				laser.muzzle = systemAt(look->look.muzzleSystem, laser.start);
				laser.impact = systemAt(look->look.targetSystem, laser.end);
				active.push_back(laser);
			}
		requests.clear();
		for (std::size_t index = 0; index < active.size();)
		{
			ActiveLaser &laser = active[index];
			const WeaponLaser *look = weapons.Of(laser.weapon);
			// Drawn every frame of its lifetime (at least once): aged after it is drawn.
			const float lifetime = std::max(laser.lifetime, 1.0f / 30.0f);
			if (look == nullptr || laser.age > lifetime)
			{
				if (particles.world != nullptr)
					for (const std::uint64_t system : {laser.muzzle, laser.impact})
						if (system != 0)
							particles.world->Stop(system);
				active[index] = active.back();
				active.pop_back();
				continue;
			}
			if (const auto start = startOf(laser, *look))
				laser.start = *start;
			AppendLaserBeams(out, look->look, laser.start, laser.end, 1.0f, laser.age, ground);
			laser.age += frame.seconds;
			++index;
		}
		for (std::size_t index = 0; index < abilityLasers.size(); ++index)
		{
			const auto &[entity, laser] = abilityLasers[index];
			const DefinitionLooks *looks = catalog.Of(abilityDefinitions[index]);
			if (looks == nullptr || !looks->laser)
				continue;
			std::string_view bone;
			if (const PresentedObject *parent = presented(laser->parent))
				if (const DefinitionLooks *unit = catalog.Of(parent->definition))
					for (const auto &[power, name] : unit->laserBones)
						if (power == laser->power)
							bone = name;
			const std::array<float, 3> end{Engine::Math::ToFloat(laser->end.x), Engine::Math::ToFloat(laser->end.y), Engine::Math::ToFloat(laser->end.z)};
			const auto start = boneOf(laser->parent, bone);
			AbilityLaserView *view = abilityViews.Get(entity);
			if (view == nullptr)
			{
				view = abilityViews.Emplace(entity);
				view->start = start.value_or(end);
				// Its flares only when its unit is seen as it starts.
				if (start)
				{
					view->muzzle = systemAt(looks->laser->muzzleSystem, view->start);
					view->impact = systemAt(looks->laser->targetSystem, end);
				}
			}
			else if (start)
				view->start = *start;
			AppendLaserBeams(out, *looks->laser, view->start, end, 1.0f, view->age, ground);
			view->age += frame.seconds;
		}
	}
};

// ParticleSystemManager::destroyAttachedSystems, once a tick after the simulation: an object whose HeightDieUpdate went
// below its DestroyAttachedParticlesAtHeight this tick destroys every particle system riding on it (ParticleSystem::destroy: they emit no more, what is out lives on; its model
// states', its damage state's, its FX lists', a crash trail, a missile's exhaust); they are not started again for what
// it shows now (only something new starts them).
struct AttachedParticleClearSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Write<ConditionEmission>, ecs::Write<FxEmission>, ecs::Write<CrashTrailEmission>, ecs::Write<DamageEmission>,
		ecs::Write<ExhaustEmission>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::ParticleClears>, ecs::Write<ParticleWorldHandle>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		const auto &clears = context.Read<engine::gameplay::ParticleClears>().entities;
		ParticleWorldHandle &particles = context.Write<ParticleWorldHandle>();
		if (clears.empty() || particles.world == nullptr)
			return;
		auto &conditions = context.Side<SideTables, ConditionEmission>();
		auto &riding = context.Side<SideTables, FxEmission>();
		auto &trails = context.Side<SideTables, CrashTrailEmission>();
		auto &damage = context.Side<SideTables, DamageEmission>();
		auto &exhausts = context.Side<SideTables, ExhaustEmission>();
		const auto destroy = [&](std::vector<AttachedSystem> &systems) {
			for (const AttachedSystem &attached : systems)
				particles.world->Stop(attached.id);
			systems.clear();
		};
		for (const ecs::Entity entity : clears)
		{
			if (ConditionEmission *emission = conditions.Get(entity))
				destroy(emission->systems);
			if (FxEmission *emission = riding.Get(entity))
				destroy(emission->systems);
			if (CrashTrailEmission *trail = trails.Get(entity))
				destroy(trail->systems);
			if (DamageEmission *emission = damage.Get(entity))
				destroy(emission->systems);
			if (ExhaustEmission *exhaust = exhausts.Get(entity); exhaust != nullptr && exhaust->id != 0)
			{
				particles.world->Stop(exhaust->id);
				exhaust->tossed = 1;
			}
		}
	}
};

struct EffectAttachmentSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DebrisLook>, ecs::Read<engine::gameplay::ObjectShroud>, ecs::Read<engine::gameplay::Transform>,
		ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Write<ConditionEmission>, ecs::Write<CrashTrailEmission>, ecs::Write<FxEmission>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Read<PresentedObjects>, ecs::Read<LookCatalog>, ecs::Read<BonePoses>,
		ecs::Write<ParticleWorldHandle>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		using namespace effect_attachment_detail;
		ParticleWorldHandle &particles = context.Write<ParticleWorldHandle>();
		if (particles.world == nullptr || particles.content == nullptr)
			return;
		const std::uint32_t serial = context.Read<PresentationFrame>().frame;
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const BonePoses &poses = context.Read<BonePoses>();
		auto &conditions = context.Side<SideTables, ConditionEmission>();
		auto &trails = context.Side<SideTables, CrashTrailEmission>();
		auto &attachedFx = context.Side<SideTables, FxEmission>();
		auto &commands = context.Commands();
		const auto lookup = context.Lookup<Lookup>();
		const std::uint32_t viewer = context.Read<PresentationFrame>().viewer;
		const auto shrouded = [&](ecs::Entity entity) {
			return lookup.IsAlive(entity) && ShroudedFromViewer(lookup.Get<engine::gameplay::ObjectShroud>(entity), viewer);
		};
		context.Read<PresentedObjects>().ForEach([&](const PresentedObject &object) {
			const bool obscured = shrouded(object.entity);
			if (FxEmission *riding = attachedFx.Get(object.entity))
			{
				riding->seenFrame = serial;
				for (const AttachedSystem &attached : riding->systems)
					particles.world->Move(attached.id, Place(object, attached));
				ObscureSystems(*particles.world, riding->systems, obscured);
			}
			// A debris piece's ParticleSystem (GenericObjectCreationNugget: attachToObject), started as it first shows and
			// riding on it from then on, as FX attachments do.
			else if (const auto *debris = lookup.Get<engine::gameplay::DebrisLook>(object.entity); debris != nullptr && debris->particleSystem != 0)
				if (const auto *definition = particles.content->particles.Find(catalog.ParticleName(debris->particleSystem)))
				{
					AttachedSystem attached;
					attached.id = particles.world->Create(*definition, Place(object, attached));
					commands.Add<FxEmission>(object.entity, FxEmission{{attached}, serial});
				}
			const DefinitionLooks *looks = catalog.Of(object.definition);
			if (looks == nullptr || object.look >= catalog.lookModels.size())
				return;
			const std::string_view model = catalog.lookModels[object.look];
			const std::span<const content::ModelState::ParticleBone> bones =
				looks->states.Empty() ? std::span<const content::ModelState::ParticleBone>{} : std::span(looks->states.states[object.state].particleBones);
			if (ConditionEmission *emission = conditions.Get(object.entity))
			{
				emission->seenFrame = serial;
				if (emission->look != object.look)
				{
					Remove(*particles.world, emission->systems);
					emission->look = Start(emission->systems, bones, object, model, poses, particles) ? object.look : ConditionEmission::NoLook;
				}
				// AnimatedParticleSysBoneClientUpdate / ParticlesAttachedToAnimatedBones (updateBonesForClientParticleSystems):
				// each at its bone as the model animates now.
				const bool animated = looks->animatedParticleBones && poses.animated && !model.empty();
				for (AttachedSystem &attached : emission->systems)
				{
					if (animated && attached.bone >= 0 && static_cast<std::size_t>(attached.bone) < bones.size())
						if (const BoneLookup now = poses.animated(object.look, object.animationSeconds, object.animationStart, bones[attached.bone].bone); now.found)
						{
							attached.local = now.position;
							attached.yaw = now.yaw;
						}
					particles.world->Move(attached.id, Place(object, attached));
				}
				// Presented, it is not obscured by the shroud (lingering included); stealth hides it (not drawn).
				SwitchSystems(*particles.world, emission->systems, object.drawn);
			}
			else if (!bones.empty())
				commands.Add<ConditionEmission>(object.entity, ConditionEmission{}); // starts next frame
			if (!object.appearance.Test(catalog.bits.dying) || looks->crashTrail.empty())
				return;
			if (CrashTrailEmission *trail = trails.Get(object.entity))
			{
				trail->seenFrame = serial;
				if (trail->started == 0)
					trail->started = Start(trail->systems, looks->crashTrail, object, model, poses, particles) ? 1u : 0u;
				for (const AttachedSystem &attached : trail->systems)
					particles.world->Move(attached.id, Place(object, attached));
				ObscureSystems(*particles.world, trail->systems, obscured);
			}
			else
				commands.Add<CrashTrailEmission>(object.entity, CrashTrailEmission{});
		});
		// Objects not presented: hidden from the viewer by the shroud, their systems are kept (a model's stopped, an
		// object's emitting nothing: doStartOrStopParticleSys / ParticleSystem::update's isShrouded) and go on where
		// they were when it shows again; otherwise (off the map, gone) they are destroyed (what is out lives on).
		for (std::size_t index = 0; index < conditions.Size(); ++index)
			if (ConditionEmission &emission = conditions.Value(index); emission.seenFrame != serial && !emission.systems.empty())
			{
				if (shrouded(conditions.Entities()[index]))
				{
					SwitchSystems(*particles.world, emission.systems, false);
					continue;
				}
				Remove(*particles.world, emission.systems);
				emission.look = ConditionEmission::NoLook;
			}
		for (std::size_t index = 0; index < trails.Size(); ++index)
			if (CrashTrailEmission &trail = trails.Value(index); trail.seenFrame != serial && !trail.systems.empty())
			{
				if (const ecs::Entity entity = trails.Entities()[index]; shrouded(entity))
				{
					FollowHidden(*particles.world, trail.systems, lookup.Get<engine::gameplay::Transform>(entity),
						LookScale(catalog, lookup.Get<engine::gameplay::DefinitionRef>(entity)));
					continue;
				}
				Remove(*particles.world, trail.systems);
				trail.started = 0;
			}
		// As the original: a system attached to an object goes with it.
		for (std::size_t index = 0; index < attachedFx.Size(); ++index)
			if (FxEmission &riding = attachedFx.Value(index); riding.seenFrame != serial && !riding.systems.empty())
			{
				if (const ecs::Entity entity = attachedFx.Entities()[index]; shrouded(entity))
				{
					FollowHidden(*particles.world, riding.systems, lookup.Get<engine::gameplay::Transform>(entity),
						LookScale(catalog, lookup.Get<engine::gameplay::DefinitionRef>(entity)));
					continue;
				}
				Remove(*particles.world, riding.systems);
			}
	}
};

// Damage states, each frame after the objects are presented (one pass: the
// particle world and the FX requests are its side effects): getting worse
// into a state plays that state's FX lists once and starts its particle
// systems (smoke, fire, sparks), which follow the object until it leaves
// the state (they stop; what is out lives on); getting better only stops
// the old ones, as the original. Objects no longer presented lose theirs.
struct DamageEffectSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Write<DamageEmission>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Health>, ecs::Read<engine::gameplay::ObjectShroud>, ecs::Read<engine::gameplay::Transform>,
		ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Read<PresentedObjects>, ecs::Read<LookCatalog>, ecs::Read<BonePoses>,
		ecs::Write<ParticleWorldHandle>, ecs::Write<FxRequests>, ecs::Write<PresentationRandom>, ecs::Write<EffectStats>>;

	static DamageLevel LevelOf(const PresentedObject &object, const LookBits &bits) noexcept
	{
		return object.appearance.Test(bits.rubble) ? DamageLevel::Rubble
			: object.appearance.Test(bits.reallyDamaged) ? DamageLevel::ReallyDamaged
			: object.appearance.Test(bits.damaged) ? DamageLevel::Damaged
												   : DamageLevel::Pristine;
	}

	void Execute(Query &, ecs::SystemContext &context) const
	{
		using namespace effect_attachment_detail;
		ParticleWorldHandle &particles = context.Write<ParticleWorldHandle>();
		if (particles.world == nullptr || particles.content == nullptr)
			return;
		const std::uint32_t serial = context.Read<PresentationFrame>().frame;
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const BonePoses &poses = context.Read<BonePoses>();
		FxRequests &fx = context.Write<FxRequests>();
		PresentationRandom &random = context.Write<PresentationRandom>();
		EffectStats &stats = context.Write<EffectStats>();
		auto &emissions = context.Side<SideTables, DamageEmission>();
		auto &commands = context.Commands();
		const auto lookup = context.Lookup<Lookup>();
		const std::uint32_t viewer = context.Read<PresentationFrame>().viewer;
		const auto shrouded = [&](ecs::Entity entity) {
			return lookup.IsAlive(entity) && ShroudedFromViewer(lookup.Get<engine::gameplay::ObjectShroud>(entity), viewer);
		};
		const auto stop = [&](DamageEmission &emission) {
			for (const AttachedSystem &attached : emission.systems)
				particles.world->Stop(attached.id);
			emission.systems.clear();
		};
		context.Read<PresentedObjects>().ForEach([&](const PresentedObject &object) {
			const DefinitionLooks *looks = catalog.Of(object.definition);
			if (looks == nullptr)
				return;
			DamageEmission *emission = emissions.Get(object.entity);
			if (emission == nullptr)
			{
				commands.Add<DamageEmission>(object.entity, DamageEmission{}); // from next frame
				return;
			}
			emission->seenFrame = serial;
			const DamageLevel level = LevelOf(object, catalog.bits);
			const auto levelIndex = static_cast<std::uint32_t>(level);
			if (emission->known == 0 || emission->level != levelIndex)
			{
				stop(*emission);
				const bool worse = emission->known != 0 ? levelIndex > emission->level : level != DamageLevel::Pristine;
				if (level != DamageLevel::Pristine)
				{
					++stats.hurt;
					stats.hurtWithEffects += looks->damage.At(level).empty() ? 0u : 1u;
				}
				emission->level = levelIndex;
				emission->known = 1;
				const std::string_view model = object.look < catalog.lookModels.size() ? std::string_view(catalog.lookModels[object.look]) : std::string_view{};
				// Its last damage's type (getLastDamageInfo; never hurt: DamageInfo's default, EXPLOSION).
				const engine::gameplay::Health *body = lookup.IsAlive(object.entity) ? lookup.Get<engine::gameplay::Health>(object.entity) : nullptr;
				const std::uint32_t lastType = body != nullptr ? body->lastDamageType : 0u;
				if (worse)
					for (const DamageEffect &effect : looks->damage.At(level))
					{
						if (lastType < 64 && (effect.types & (std::uint64_t{1} << lastType)) == 0)
							continue;
						// At its bone (a random one of a family), else at its offset (the original's fallback).
						AttachedSystem attached{0, effect.placement.offset, 0.0f};
						if (!effect.placement.bone.empty() && poses.locate)
							if (const auto found = poses.locate(model, effect.placement.bone, effect.placement.randomBone); !found.empty())
							{
								random.pick = random.pick * 1664525u + 1013904223u;
								attached.local = found[effect.placement.randomBone ? (random.pick >> 8) % found.size() : 0];
							}
						const auto at = Place(object, attached);
						if (effect.kind == DamageEffect::Kind::FxList)
						{
							const auto point = at.Point({0, 0, 0});
							fx.pending.push_back({effect.name, {point[0], point[1], point[2]}, object.facing, 0.0f, object.entity});
						}
						else if (const auto *definition = particles.content->particles.Find(effect.name))
						{
							attached.id = particles.world->Create(*definition, at);
							emission->systems.push_back(attached);
						}
					}
			}
			for (const AttachedSystem &attached : emission->systems)
				particles.world->Move(attached.id, Place(object, attached));
			// TransitionDamageFX's systems ride the object (attachToObject): none emitted while the viewer sees it
			// fogged or shrouded (ParticleSystem::update's isShrouded).
			ObscureSystems(*particles.world, emission->systems, shrouded(object.entity));
		});
		for (std::size_t index = 0; index < emissions.Size(); ++index)
			if (DamageEmission &emission = emissions.Value(index); emission.seenFrame != serial && emission.known != 0)
			{
				// Hidden by the shroud: kept, emitting nothing, until it shows again.
				if (const ecs::Entity entity = emissions.Entities()[index]; shrouded(entity))
				{
					FollowHidden(*particles.world, emission.systems, lookup.Get<engine::gameplay::Transform>(entity),
						LookScale(catalog, lookup.Get<engine::gameplay::DefinitionRef>(entity)));
					continue;
				}
				stop(emission);
				emission.known = 0;
			}
	}
};

// The frame's FX requests played (one pass: the particle world, the sound
// and shake requests are its side effects); systems they attach to their
// object ride on it (FxEmission) from the next frame. A request on an object
// sounds for its controlling player (SoundFXNugget::doFXObj); FXListAtBonePos
// finds the object's bones as presented this frame (CurrentBoneTransforms);
// CreateAtGroundHeight stands on the bridge deck a spot is nearest when the
// world has decks (TerrainLogic::getLayerForDestination, getLayerHeight).
struct FxPlaybackSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Owner>>;
	using SideTables = ecs::SideTables<ecs::Write<FxEmission>>;
	using Resources = ecs::Resources<ecs::Read<TerrainHeightHandle>, ecs::Write<ParticleWorldHandle>, ecs::Write<FxRequests>, ecs::Write<SoundRequests>,
		ecs::Write<ShakeRequests>, ecs::Write<PresentationRandom>, ecs::Write<EffectStats>, ecs::Write<LightPulses>, ecs::Write<ScorchMarks>,
		ecs::Write<Tracers>, ecs::Read<PresentedObjects>, ecs::Read<BonePoses>, ecs::Read<engine::gameplay::DeckSurfaces>,
		ecs::Read<engine::gameplay::GroundHeight>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		ParticleWorldHandle &particles = context.Write<ParticleWorldHandle>();
		Tracers &tracers = context.Write<Tracers>();
		auto &pulses = context.Write<LightPulses>().live;
		auto &scorches = context.Write<ScorchMarks>();
		FxRequests &requests = context.Write<FxRequests>();
		if (particles.world == nullptr || particles.content == nullptr)
		{
			requests.pending.clear();
			return;
		}
		const GroundHeightAt &ground = context.Read<TerrainHeightHandle>().at;
		auto &sounds = context.Write<SoundRequests>().pending;
		auto &shakes = context.Write<ShakeRequests>().pending;
		auto &random = context.Write<PresentationRandom>().engine;
		EffectStats &stats = context.Write<EffectStats>();
		auto &riding = context.Side<SideTables, FxEmission>();
		const auto owners = context.Lookup<Lookup>();
		FxSurroundings around;
		const PresentedObjects &presented = context.Read<PresentedObjects>();
		const BonePoses &poses = context.Read<BonePoses>();
		around.bones = [&](ecs::Entity object, std::string_view bone, int start) {
			const PresentedObject *found = nullptr;
			presented.ForEach([&](const PresentedObject &candidate) {
				if (candidate.entity == object)
					found = &candidate;
			});
			return found != nullptr ? CurrentBoneTransforms(*found, poses, bone, start) : std::vector<std::array<float, 12>>{};
		};
		const auto *decks = context.Find<engine::gameplay::DeckSurfaces>();
		const auto *logicGround = context.Find<engine::gameplay::GroundHeight>();
		if (decks != nullptr && logicGround != nullptr && !decks->decks.empty())
			around.layerHeight = [&](float x, float y, float z) {
				const auto fixed = [](float value) { return Engine::Math::Fixed::FromRaw(std::llround(static_cast<double>(value) * 65536.0)); };
				const Engine::Math::FixedVector3 at{fixed(x), fixed(y), fixed(z)};
				const std::uint8_t layer = engine::gameplay::LayerForDestination(*decks, *logicGround, at);
				if (layer == engine::gameplay::GroundLayer)
					return ground ? ground(x, y) : Engine::Math::ToFloat(logicGround->At(at.XY()));
				return Engine::Math::ToFloat(engine::gameplay::LayerHeight(*decks, *logicGround, at.XY(), layer));
			};
		std::vector<std::pair<ecs::Entity, std::vector<AttachedSystem>>> added; // to objects without any yet
		std::vector<FxAttachment> attached;
		for (FxRequest &request : requests.pending)
		{
			attached.clear();
			if (request.scorchRadius > 0.0f)
			{
				AddScorch(scorches, ScorchMark{request.at, request.scorchRadius, 0});
				if (request.fx.empty())
					continue;
			}
			if (request.object.IsValid() && request.owner == SoundRequest::NoOwner && owners.IsAlive(request.object))
				if (const auto *owner = owners.Get<engine::gameplay::Owner>(request.object))
					request.owner = owner->player;
			stats.fxPlayed += PlayFx(*particles.content, *particles.world, random, ground, request, sounds, shakes, &attached, &pulses, 0, &scorches, &tracers,
								  &around)
				? 1u
				: 0u;
			if (attached.empty())
				continue;
			std::vector<AttachedSystem> *systems = nullptr;
			if (FxEmission *emission = riding.Get(request.object))
				systems = &emission->systems;
			else
			{
				auto found = std::find_if(added.begin(), added.end(), [&](const auto &entry) { return entry.first == request.object; });
				systems = found != added.end() ? &found->second : &added.emplace_back(request.object, std::vector<AttachedSystem>{}).second;
			}
			for (const FxAttachment &system : attached)
				systems->push_back({system.id, system.local, system.yaw});
		}
		for (auto &[object, systems] : added)
			context.Commands().Add<FxEmission>(object, FxEmission{std::move(systems), 0});
		requests.pending.clear();
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::DamageEffectSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.damage_effects";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::AttachedParticleClearSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.attached_particle_clears";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::LaserSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.lasers";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::MissileIgnitionSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.missile_ignition";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<generalszh::presentation::ExhaustSampleSystem>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::ExhaustSampleSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.exhaust_sample";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::ExhaustSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.exhaust";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::FireFxPlacementSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.fire_fx_placement";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::FxPlaybackSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.fx_playback";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::EffectAttachmentSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.effect_attachments";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The composition orders it after the objects are presented this frame.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
