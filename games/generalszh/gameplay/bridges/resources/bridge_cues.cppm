export module games.generalszh.gameplay.bridges.resources.bridge_cues;
import std;

export import engine.ecs.core.component_registry;
import engine.ecs.system.system;
export import Engine.Core.Math.FixedVector;

// What the tick's bridges gave the presentation: a transition's sound (DamagedToSound / RepairedToSound) where the
// bridge stands, and the FX lists played (a transition's area effects, a death's BridgeDieFX) where they went off. The
// tick's only: cleared as each tick starts.
export namespace generalszh::gameplay
{
struct BridgeCue
{
	std::string name;
	Engine::Math::FixedVector3 at;
	bool sound{false}; // else an FX list
};

struct BridgeCues
{
	std::vector<BridgeCue> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::BridgeCues>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.bridge_cues";
};
}
