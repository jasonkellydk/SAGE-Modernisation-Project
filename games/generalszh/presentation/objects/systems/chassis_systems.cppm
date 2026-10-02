export module games.generalszh.presentation.objects.systems.chassis_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.physics.resources.physics_settings;
export import engine.gameplay.rts.movement.components.locomotion;
export import engine.gameplay.rts.death.components.dying;
export import engine.gameplay.rts.combat.resources.shots;
export import games.generalszh.presentation.objects.algorithms.chassis_motion;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
import engine.ecs.core.component_registry;
import Engine.Core.Math.FixedPresentation;

// Vehicles' bodies rocking on their suspension (the original's
// Drawable::calcPhysicsXform, a logic frame at a time), once a tick: from
// where it went since the last tick (its velocity, and the change of it: its
// acceleration), the ground's slope under it (the terrain's normal), whether
// it is well off the ground; the pose of the last two ticks, for frames to
// blend between. A side table on the simulation's own entities.
export namespace generalszh::presentation
{
struct ChassisMotion
{
	ChassisState state;
	ChassisPose previous;
	ChassisPose current;
	std::array<float, 3> lastPosition{};
	std::array<float, 3> lastVelocity{};
	std::uint32_t samples{0};
	// Drawable::calcPhysicsXformWheels's TWheelInfo: ticks in the air so far, and (on the tick it lands) how many it
	// was up; whether it is significantly above the ground; its last tick's acceleration.
	std::uint32_t airborneCounter{0};
	std::uint32_t framesAirborne{0};
	std::uint32_t airborne{0};
	std::array<float, 3> acceleration{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::presentation::ChassisMotion>
{
	static constexpr std::string_view StableName = "generalszh.presentation.chassis_motion";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Transient;
	static constexpr ComponentStorage Storage = ComponentStorage::SideTable;
};
}

export namespace generalszh::presentation
{
struct ChassisSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Optional<engine::gameplay::Locomotion>,
		ecs::Optional<engine::gameplay::Dying>, ecs::Exclude<engine::gameplay::OffMap>>;
	using SideTables = ecs::SideTables<ecs::Write<ChassisMotion>>;
	using Resources = ecs::Resources<ecs::Read<LookCatalog>, ecs::Read<TerrainHeightHandle>, ecs::Read<engine::gameplay::PhysicsSettings>,
		ecs::Write<PresentationRandom>, ecs::Read<engine::gameplay::FiredShots>, ecs::Read<WeaponRecoils>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Transform>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const TerrainHeightHandle &terrain = context.Read<TerrainHeightHandle>();
		const float significant = Engine::Math::ToFloat(context.Read<engine::gameplay::PhysicsSettings>().SignificantHeight());
		PresentationRandom &random = context.Write<PresentationRandom>();
		auto &table = context.Side<SideTables, ChassisMotion>();
		const auto height = [&](float x, float y) { return terrain.at ? terrain.at(x, y) : 0.0f; };
		// Drawable::handleWeaponFireFX: the tick's shots rock their firers back from where they fired (WeaponRecoil,
		// in the direction from the firer to its aim, relative to its facing, turned about).
		const WeaponRecoils &recoils = context.Read<WeaponRecoils>();
		const auto lookup = context.Lookup<Lookup>();
		context.Read<engine::gameplay::FiredShots>().ForEach([&](const engine::gameplay::Shot &shot) {
			const float amount = recoils.Of(shot.weapon);
			if (amount == 0.0f || !lookup.IsAlive(shot.source))
				return;
			ChassisMotion *motion = table.Get(shot.source);
			const auto *firer = lookup.Get<engine::gameplay::Transform>(shot.source);
			if (motion == nullptr || firer == nullptr)
				return;
			const float facing = static_cast<float>(firer->facing.units) * 6.283185307179586f / 4294967296.0f;
			Recoil(motion->state, amount,
				std::atan2(Engine::Math::ToFloat(shot.aim.y - firer->position.y), Engine::Math::ToFloat(shot.aim.x - firer->position.x)), facing);
		});
		query.ForEachChunk([&](auto chunk) {
			const auto transforms = chunk.template Get<engine::gameplay::Transform>();
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			const auto motions = chunk.template Get<engine::gameplay::Locomotion>();
			const auto dyings = chunk.template Get<engine::gameplay::Dying>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < transforms.size(); ++row)
			{
				const DefinitionLooks *looks = catalog.Of(definitions[row].index);
				if (looks == nullptr || !looks->chassis || looks->chassis->kind == content::ChassisKind::None)
					continue;
				const auto &transform = transforms[row];
				const std::array<float, 3> at{Engine::Math::ToFloat(transform.position.x), Engine::Math::ToFloat(transform.position.y),
					Engine::Math::ToFloat(transform.position.z)};
				ChassisMotion *motion = table.Get(entities[row]);
				if (motion == nullptr)
				{
					motion = table.Emplace(entities[row]);
					motion->lastPosition = at;
				}
				ChassisInput in;
				const float facing = static_cast<float>(transform.facing.units) * 6.283185307179586f / 4294967296.0f;
				in.dir = {std::cos(facing), std::sin(facing)};
				for (std::size_t axis = 0; axis < 3; ++axis)
				{
					in.velocity[axis] = motion->samples > 0 ? at[axis] - motion->lastPosition[axis] : 0.0f;
					in.acceleration[axis] = motion->samples > 1 ? in.velocity[axis] - motion->lastVelocity[axis] : 0.0f;
				}
				// The terrain's normal under it (the slope of its triangle, as central differences).
				const float d = 1.0f;
				std::array<float, 3> normal{height(at[0] - d, at[1]) - height(at[0] + d, at[1]), height(at[0], at[1] - d) - height(at[0], at[1] + d), 2.0f * d};
				const float length = std::sqrt(normal[0] * normal[0] + normal[1] * normal[1] + normal[2] * normal[2]);
				for (float &component : normal)
					component /= length;
				in.groundNormal = normal;
				in.height = at[2] - height(at[0], at[1]);
				in.airborne = in.height > significant;
				in.motive = dyings.empty() && !motions.empty();
				in.speed = std::hypot(in.velocity[0], in.velocity[1], in.velocity[2]);
				in.maxSpeed = motions.empty() ? 0.0f : Engine::Math::ToFloat(motions[row].locomotor.maxSpeed);
				in.majorRadius = looks->majorRadius;
				in.minorRadius = looks->minorRadius;
				in.bounce = static_cast<int>(std::uniform_int_distribution<int>(0, 3)(random.engine));
				motion->previous = motion->current;
				motion->current = StepChassis(motion->state, *looks->chassis, in);
				motion->lastVelocity = in.velocity;
				motion->lastPosition = at;
				motion->acceleration = in.acceleration;
				motion->airborne = in.airborne ? 1u : 0u;
				if (in.airborne)
				{
					motion->framesAirborne = 0;
					++motion->airborneCounter;
				}
				else
				{
					motion->framesAirborne = motion->airborneCounter;
					motion->airborneCounter = 0;
				}
				++motion->samples;
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::ChassisSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.chassis";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
