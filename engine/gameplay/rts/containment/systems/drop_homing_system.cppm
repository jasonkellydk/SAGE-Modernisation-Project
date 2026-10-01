export module engine.gameplay.rts.containment.systems.drop_homing_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.containment.components.drop_homing;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.resources.ground_height;
export import engine.gameplay.common.physics.resources.physics_settings;

// SmartBombTargetHomingUpdate::update for every homing payload, chunk-parallel: once told its spot and while
// significantly above the ground (isSignificantlyAboveTerrain), it is put CourseCorrectionScalar of the way from its spot
// to where it is (x and y; its height its own): target x (1 - keep) + position x keep.
export namespace engine::gameplay
{
struct DropHomingSystem
{
	using Query = ecs::Query<ecs::Read<DropHoming>, ecs::Write<Transform>>;
	using Resources = ecs::Resources<ecs::Read<GroundHeight>, ecs::Read<PhysicsSettings>>;

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		using Engine::Math::Fixed;
		const GroundHeight &ground = context.Read<GroundHeight>();
		const Fixed significant = context.Read<PhysicsSettings>().SignificantHeight();
		const auto homings = chunk.Get<DropHoming>();
		auto transforms = chunk.Get<Transform>();
		for (std::size_t row = 0; row < homings.size(); ++row)
		{
			const DropHoming &homing = homings[row];
			Engine::Math::FixedVector3 &position = transforms[row].position;
			if (homing.received == 0 || position.z - ground.At(position.XY()) <= significant)
				continue;
			const Fixed keep = std::clamp(homing.keep, Fixed{}, Fixed::One());
			const Fixed toward = Fixed::One() - keep;
			position.x = homing.target.x * toward + position.x * keep;
			position.y = homing.target.y * toward + position.y * keep;
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::DropHomingSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.drop_homing";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
