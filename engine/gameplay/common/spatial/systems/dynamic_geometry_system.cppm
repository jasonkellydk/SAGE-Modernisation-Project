export module engine.gameplay.common.spatial.systems.dynamic_geometry_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.spatial.components.dynamic_geometry;
export import engine.gameplay.common.spatial.components.bounding_volume;

// DynamicGeometryInfoUpdate::update for every body that changes size, chunk-parallel: finished, nothing; before it starts,
// its delay counts down (starting the tick it runs out); then its size is its initial plus time active / TransitionTime of
// the way to its final (setGeometryInfo: its bounding volume follows), and time active goes on; past TransitionTime it
// reverses once if it should (its final size its start, its initial its end, time from zero), else it is finished.
export namespace engine::gameplay
{
struct DynamicGeometrySystem
{
	using Query = ecs::Query<ecs::Write<DynamicGeometry>, ecs::OptionalWrite<BoundingVolume>>;

	static void Step(DynamicGeometry &geometry, BoundingVolume *volume) noexcept
	{
		using Engine::Math::Fixed;
		if (geometry.finished != 0)
			return;
		if (geometry.started == 0)
		{
			if (geometry.delayLeft > 0)
				--geometry.delayLeft;
			if (geometry.delayLeft > 0)
				return;
			geometry.started = 1;
		}
		const Fixed active = Fixed::FromInt(geometry.timeActive), span = Fixed::FromInt(std::max<std::uint32_t>(geometry.transitionTime, 1));
		const auto lerp = [&](Fixed from, Fixed to) { return from + (to - from) * active / span; };
		geometry.height = lerp(geometry.initialHeight, geometry.finalHeight);
		geometry.major = lerp(geometry.initialMajor, geometry.finalMajor);
		geometry.minor = lerp(geometry.initialMinor, geometry.finalMinor);
		if (volume != nullptr)
		{
			const bool sphere = geometry.shape == geometry_shape::Sphere;
			volume->sphereRadius = geometry.SphereRadius();
			volume->circleRadius = geometry.CircleRadius();
			volume->centerLift = sphere ? Fixed{} : geometry.height / Fixed::FromInt(2);
			volume->below = sphere ? geometry.major : Fixed{};
			volume->above = sphere ? geometry.major : geometry.height;
		}
		++geometry.timeActive;
		if (geometry.timeActive <= geometry.transitionTime)
			return;
		if (geometry.reverse != 0)
		{
			geometry.switched = 1;
			geometry.timeActive = 0;
			geometry.reverse = 0;
			std::swap(geometry.initialHeight, geometry.finalHeight);
			std::swap(geometry.initialMajor, geometry.finalMajor);
			std::swap(geometry.initialMinor, geometry.finalMinor);
		}
		else
			geometry.finished = 1;
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &) const
	{
		auto geometries = chunk.Get<DynamicGeometry>();
		auto volumes = chunk.Get<BoundingVolume>();
		for (std::size_t row = 0; row < geometries.size(); ++row)
			Step(geometries[row], volumes.empty() ? nullptr : &volumes[row]);
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::DynamicGeometrySystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.dynamic_geometry";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
