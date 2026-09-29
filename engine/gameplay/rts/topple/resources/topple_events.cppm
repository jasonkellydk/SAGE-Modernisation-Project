export module engine.gameplay.rts.topple.resources.topple_events;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
export import Engine.Core.Math.FixedAngle;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// Toppling's settings (how the fallen die) and its events this tick (starts
// and bounces, per chunk), for the game to play effects for.
export namespace engine::gameplay
{
struct ToppleSettings
{
	std::uint32_t toppledDeathType{0};
};

struct ToppleEvent
{
	enum class Kind : std::uint8_t
	{
		Started,
		Bounced,
	};
	ecs::Entity entity;
	Kind kind{Kind::Started};
	Engine::Math::FixedVector3 position;
	Engine::Math::TurnAngle facing;
};

struct ToppleEvents : ecs::ChunkOutputs<ToppleEvent>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::ToppleSettings>
{
	static constexpr std::string_view StableName = "engine.gameplay.topple_settings";
};
template<>
struct ResourceTraits<engine::gameplay::ToppleEvents>
{
	static constexpr std::string_view StableName = "engine.gameplay.topple_events";
};
}

