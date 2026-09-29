export module engine.gameplay.rts.aircraft.components.airfield;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
import engine.ecs.core.component_registry;

// Where an airfield parks and launches its jets (the original's
// ParkingPlaceBehavior), in the world: per parking space the hangar a new
// jet starts in, its parking spot and the runway prep point in front of it;
// per runway its start and end. Set when it is placed; jets keep their own
// space and runway, so the airfield itself never changes.
export namespace engine::gameplay
{
struct ParkingSpace
{
	Engine::Math::FixedVector3 hangar;
	Engine::Math::FixedVector3 parking;
	Engine::Math::FixedVector3 prep;
	Engine::Math::TurnAngle hangarFacing;
	Engine::Math::TurnAngle parkingFacing;
	std::uint32_t runway{0};
	std::uint32_t reserved{0};
};

struct RunwayPath
{
	Engine::Math::FixedVector3 start;
	Engine::Math::FixedVector3 end;
};

struct Airfield
{
	static constexpr std::uint32_t MaxSpaces = 10;
	static constexpr std::uint32_t MaxRunways = 4;
	std::array<ParkingSpace, MaxSpaces> spaces{};
	std::array<RunwayPath, MaxRunways> runways{};
	std::uint32_t spaceCount{0};
	std::uint32_t runwayCount{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Airfield>
{
	static constexpr std::string_view StableName = "engine.gameplay.airfield";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
