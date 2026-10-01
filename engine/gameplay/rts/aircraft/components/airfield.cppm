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
// A flight deck (FlightDeckBehavior: a carrier) has more: per runway a landing strip of its own (landStart to
// landEnd; an airfield lands on its runway backwards), the taxi points a landed jet rolls through to its space and the
// creation points a new one comes out of the hangar by (the first is the hangar itself); its deck stands `deckHeight`
// over the terrain (LandingDeckHeightOffset: jets on it stand there), and only the front row takes off (`frontRow`: a
// jet further back waits to be moved up).
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
	static constexpr std::uint32_t MaxTaxi = 8;
	static constexpr std::uint32_t MaxCreation = 4;
	Engine::Math::FixedVector3 start;
	Engine::Math::FixedVector3 end;
	// A flight deck's (else as start and end: an airfield lands from its end back to its start).
	Engine::Math::FixedVector3 landStart;
	Engine::Math::FixedVector3 landEnd;
	std::array<Engine::Math::FixedVector3, MaxTaxi> taxi{};
	std::array<Engine::Math::FixedVector3, MaxCreation> creation{};
	std::uint32_t taxiCount{0};
	std::uint32_t creationCount{0};
	std::uint32_t landing{0}; // 1: landStart / landEnd hold its own landing strip
	std::uint32_t reserved{0};
};

struct Airfield
{
	static constexpr std::uint32_t MaxSpaces = 20;
	static constexpr std::uint32_t MaxRunways = 4;
	std::array<ParkingSpace, MaxSpaces> spaces{};
	std::array<RunwayPath, MaxRunways> runways{};
	std::uint32_t spaceCount{0};
	std::uint32_t runwayCount{0};
	Engine::Math::Fixed deckHeight;
	std::uint32_t frontRow{0}; // a flight deck: only spaces below runwayCount take off
	std::uint32_t reserved{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Airfield>
{
	static constexpr std::string_view StableName = "engine.gameplay.airfield";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
