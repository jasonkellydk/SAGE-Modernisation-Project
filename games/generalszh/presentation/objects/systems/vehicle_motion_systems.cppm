export module games.generalszh.presentation.objects.systems.vehicle_motion_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.movement.components.move_order;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.appearance.components.appearance;
export import games.generalszh.presentation.objects.components.vehicle_motion;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.animation.tread_roll;
export import games.generalszh.presentation.objects.systems.chassis_systems;
import games.generalszh.content.objects.model_draw;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.spatial.components.object_shroud;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.rts.stealth.components.stealth;
import Engine.Core.Math.FixedPresentation;

// Vehicles in motion, as presentation systems over the simulation's own
// entities (reading its components, writing side tables only):
// - once a tick, the motion sample: how far and which way each vehicle moved
//   since the last tick (in parallel per chunk; first sight adds its tables);
// - each frame, on game time: treads roll and tires turn by that motion (in
//   parallel per chunk), and debris and dust emit while it moves (one pass:
//   the particle world is the side effect).
export namespace generalszh::presentation
{
namespace vehicle_motion_detail
{
constexpr float RadiansPerUnit = 6.283185307179586f / 4294967296.0f;
}

struct MotionSampleSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>,
		ecs::Optional<engine::gameplay::Appearance>, ecs::Optional<engine::gameplay::MoveOrder>, ecs::Optional<engine::gameplay::Locomotion>>;
	using SideTables = ecs::SideTables<ecs::Write<MotionSample>>;
	using Resources = ecs::Resources<ecs::Read<MotionLooks>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		using namespace vehicle_motion_detail;
		const MotionLooks &looks = context.Read<MotionLooks>();
		auto &samples = context.Side<SideTables, MotionSample>();
		const auto transforms = chunk.Get<engine::gameplay::Transform>();
		const auto definitions = chunk.Get<engine::gameplay::DefinitionRef>();
		const auto appearances = chunk.Get<engine::gameplay::Appearance>();
		const auto orders = chunk.Get<engine::gameplay::MoveOrder>();
		const auto locomotions = chunk.Get<engine::gameplay::Locomotion>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < transforms.size(); ++row)
		{
			const MotionLook *look = looks.Of(definitions[row].index);
			if (look == nullptr || !look->Moves())
				continue;
			const auto &at = transforms[row].position;
			const std::array<float, 3> position{Engine::Math::ToFloat(at.x), Engine::Math::ToFloat(at.y), Engine::Math::ToFloat(at.z)};
			const std::uint32_t units = transforms[row].facing.units;
			const float facing = static_cast<float>(units) * RadiansPerUnit;
			const std::uint32_t dying = !appearances.empty() && appearances[row].Test(looks.dyingBit) ? 1u : 0u;
			MotionSample *sample = samples.Get(entities[row]);
			if (sample == nullptr)
			{
				// First seen: its look state starts here (added at playback).
				auto &commands = context.Commands();
				commands.Add<MotionSample>(entities[row], MotionSample{position, facing, 0.0f, 0.0f, 0, units, dying});
				if (look->treadRate > 0.0f)
					commands.Add<TreadRoll>(entities[row], TreadRoll{});
				if (!look->wheelBones.empty())
					commands.Add<WheelRoll>(entities[row], WheelRoll{});
				if (!look->motionSystems.empty())
					commands.Add<MotionEmission>(entities[row], MotionEmission{});
				continue;
			}
			const float dx = position[0] - sample->position[0], dy = position[1] - sample->position[1];
			sample->speed = std::sqrt(dx * dx + dy * dy);
			sample->along = dx * std::cos(facing) + dy * std::sin(facing);
			sample->turn = static_cast<std::int32_t>(units - sample->facingUnits);
			sample->position = position;
			sample->facing = facing;
			sample->facingUnits = units;
			sample->dying = dying;
			// Locomotor::locoUpdate_moveTowardsPosition applies its motive force each tick it drives the object on.
			const bool driven = !orders.empty() && orders[row].mode != engine::gameplay::MoveMode::Idle && (dx != 0.0f || dy != 0.0f);
			constexpr std::uint16_t MotiveFrames = 10; // LOGICFRAMES_PER_SECOND / 3
			sample->motiveTicks = driven ? MotiveFrames : static_cast<std::uint16_t>(sample->motiveTicks > 0 ? sample->motiveTicks - 1 : 0);
			sample->turning = locomotions.empty() ? std::int8_t{0} : locomotions[row].turning;
		}
	}
};

struct TreadRollSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Read<MotionSample>, ecs::Write<TreadRoll>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Read<MotionLooks>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const float seconds = context.Read<PresentationFrame>().seconds;
		const MotionLooks &looks = context.Read<MotionLooks>();
		const auto &samples = context.SideRead<SideTables, MotionSample>();
		auto &treads = context.Side<SideTables, TreadRoll>();
		const auto definitions = chunk.Get<engine::gameplay::DefinitionRef>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < definitions.size(); ++row)
		{
			TreadRoll *tread = treads.Get(entities[row]);
			const MotionSample *sample = tread != nullptr ? samples.Get(entities[row]) : nullptr;
			const MotionLook *look = looks.Of(definitions[row].index);
			if (sample == nullptr || look == nullptr)
				continue;
			const float fraction = look->maxSpeed > 0.0f ? sample->speed / look->maxSpeed : 0.0f;
			RollTreads(tread->offsets, {fraction, sample->turn, seconds}, look->treadRate, look->treadPivot, look->treadDrive);
		}
	}
};

struct WheelRollSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Read<MotionSample>, ecs::Write<WheelRoll>, ecs::Read<MotionEmission>>;
	using Resources = ecs::Resources<ecs::Read<PresentationFrame>, ecs::Read<MotionLooks>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const float seconds = context.Read<PresentationFrame>().seconds;
		const MotionLooks &looks = context.Read<MotionLooks>();
		const auto &samples = context.SideRead<SideTables, MotionSample>();
		auto &wheels = context.Side<SideTables, WheelRoll>();
		const auto &emissions = context.SideRead<SideTables, MotionEmission>();
		const auto definitions = chunk.Get<engine::gameplay::DefinitionRef>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < definitions.size(); ++row)
		{
			WheelRoll *wheel = wheels.Get(entities[row]);
			const MotionSample *sample = wheel != nullptr ? samples.Get(entities[row]) : nullptr;
			const MotionLook *look = looks.Of(definitions[row].index);
			if (sample == nullptr || look == nullptr)
				continue;
			wheel->angle = RollWheels(wheel->angle, sample->along, look->wheelMultiplier, seconds);
			// W3DTruckDraw: the rear tires also turn PowerslideRotationAddition more a tick while it powerslides (the other
			// way reversing).
			const MotionEmission *emission = emissions.Get(entities[row]);
			const float slide = emission != nullptr && emission->powersliding != 0 ? look->powerslideAddition : 0.0f;
			wheel->rearAngle = RollWheels(wheel->rearAngle, sample->along + (sample->along < 0.0f ? -slide : slide), look->wheelMultiplier, seconds);
			SteerWheels(*wheel, {sample->turn, sample->along, seconds}, look->wheelTurn, look->cabFactor, look->trailerFactor, look->swingDamping);
		}
	}
};

struct MotionEmitterSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Optional<engine::gameplay::Owner>, ecs::Optional<engine::gameplay::ObjectShroud>,
		ecs::Optional<engine::gameplay::OffMap>, ecs::Optional<engine::gameplay::Stealth>>;
	using SideTables = ecs::SideTables<ecs::Read<MotionSample>, ecs::Write<MotionEmission>, ecs::Read<ChassisMotion>>;
	using Resources = ecs::Resources<ecs::Read<MotionLooks>, ecs::Write<ParticleWorldHandle>, ecs::Write<SoundRequests>, ecs::Read<PresentationFrame>,
		ecs::Read<engine::gameplay::Relationships>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const MotionLooks &looks = context.Read<MotionLooks>();
		ParticleWorldHandle &particles = context.Write<ParticleWorldHandle>();
		if (particles.world == nullptr || particles.content == nullptr)
			return;
		auto &sounds = context.Write<SoundRequests>().pending;
		const auto &samples = context.SideRead<SideTables, MotionSample>();
		const auto &chassis = context.SideRead<SideTables, ChassisMotion>();
		auto &emissions = context.Side<SideTables, MotionEmission>();
		const PresentationFrame &frame = context.Read<PresentationFrame>();
		const auto &relationships = context.Read<engine::gameplay::Relationships>();
		query.ForEachChunk([&](auto chunk) {
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto owners = chunk.template Get<engine::gameplay::Owner>();
			const auto shrouds = chunk.template Get<engine::gameplay::ObjectShroud>();
			const auto away = chunk.template Get<engine::gameplay::OffMap>();
			const auto stealths = chunk.template Get<engine::gameplay::Stealth>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < definitions.size(); ++row)
			{
				MotionEmission *emission = emissions.Get(entities[row]);
				const MotionSample *sample = emission != nullptr ? samples.Get(entities[row]) : nullptr;
				const MotionLook *look = looks.Of(definitions[row].index);
				if (sample == nullptr || look == nullptr)
					continue;
				auto &world = *particles.world;
				if (sample->dying != 0)
				{
					// Gone: what is out lives on.
					for (std::uint32_t index = 0; index < emission->count; ++index)
						world.Stop(emission->systems[index]);
					emission->count = 0;
					emission->powersliding = 0;
					continue;
				}
				const auto at = engine::effects::EmitterTransform::At(sample->position[0], sample->position[1], sample->position[2], sample->facing);
				// Its emitters, made once and then switched on and off (createTreadEmitters / createWheelEmitters); one
				// switched off goes once its particles are gone and is made again when switched back on.
				if (emission->count == 0)
					for (std::size_t system = 0; system < look->motionSystems.size(); ++system)
						if (const auto *definition = particles.content->particles.Find(look->motionSystems[system]);
							definition != nullptr && emission->count < MotionEmission::MaxSystems)
						{
							emission->roles[emission->count] = system < look->motionRoles.size() ? look->motionRoles[system] : 0;
							emission->sources[emission->count] = static_cast<std::uint8_t>(system);
							emission->systems[emission->count] = world.Create(*definition, at);
							++emission->count;
						}
				const MotionScale scale = ScaleMotionEmitters(sample->speed);
				// W3DTankDraw: debris while it moves at all on the ground (speed squared over 0.00001), and it is neither hidden
				// (isDrawableEffectivelyHidden: inside something, or stealthed out of the viewer's sight) nor fully obscured by
				// the viewer's shroud (setFullyObscuredByShroud: fogged or worse); hidden or obscured it stops (stopMoveDebris).
				const bool viewed = frame.viewer != PresentationFrame::NoViewer && frame.viewer < 64;
				const bool obscured = viewed && !shrouds.empty() && !shrouds[row].SeenBy(frame.viewer);
				const bool stealthHidden = viewed && !stealths.empty() && stealths[row].Has(engine::gameplay::stealth_flag::Stealthed) &&
					!stealths[row].Has(engine::gameplay::stealth_flag::Detected) && !owners.empty() && !relationships.Allies(owners[row].player, frame.viewer);
				const bool treadsMoving = sample->speed * sample->speed > 0.00001f && away.empty() && !stealthHidden && !obscured;
				// W3DTruckDraw: its wheel emitters while its locomotor pushes it on the ground (reversing, its speed counts negative).
				const ChassisMotion *motion = chassis.Get(entities[row]);
				const std::array<float, 2> velocity = motion != nullptr ? std::array<float, 2>{motion->lastVelocity[0], motion->lastVelocity[1]} : std::array<float, 2>{};
				const std::array<float, 2> acceleration = motion != nullptr ? std::array<float, 2>{motion->acceleration[0], motion->acceleration[1]} : std::array<float, 2>{};
				// Its locomotor pushing it (isMotive) and turning it (getTurning).
				const TruckEmitters truck = ScaleTruckEmitters(sample->motiveTicks > 0, motion != nullptr && motion->airborne != 0,
					sample->along < 0.0f ? -sample->speed : sample->speed, velocity, acceleration, sample->turning != 0, motion != nullptr ? motion->framesAirborne : 0u);
				bool landed = false;
				for (std::uint32_t index = 0; index < emission->count; ++index)
				{
					auto &id = emission->systems[index];
					const auto role = static_cast<content::MotionEmitterRole>(emission->roles[index]);
					const auto run = [&](bool on) {
						if (!on)
							world.Pause(id);
						else if (world.Alive(id))
							world.Resume(id);
						else if (const auto *definition = particles.content->particles.Find(look->motionSystems[emission->sources[index]]))
							id = world.Create(*definition, at);
					};
					world.Move(id, at);
					switch (role)
					{
					case content::MotionEmitterRole::TreadDebris:
						run(treadsMoving);
						world.SetVelocityMultiplier(id, scale.debrisVelocity);
						world.SetBurstCountMultiplier(id, scale.debrisCount);
						break;
					case content::MotionEmitterRole::Dust:
						run(truck.dust);
						if (truck.dust)
						{
							world.SetSizeMultiplier(id, truck.dustSize);
							if (truck.landing)
							{
								world.Trigger(id);
								landed = true;
							}
						}
						break;
					case content::MotionEmitterRole::DirtSpray:
						run(truck.dirt);
						break;
					case content::MotionEmitterRole::Powerslide:
						run(truck.powerslide);
						break;
					}
				}
				emission->powersliding = truck.powerslide ? 1u : 0u;
				if (landed && !look->landingSound.empty())
					sounds.push_back({look->landingSound, sample->position});
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::MotionSampleSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.motion_sample";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::TreadRollSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.tread_roll";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::WheelRollSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.wheel_roll";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::MotionEmitterSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.motion_emitters";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
