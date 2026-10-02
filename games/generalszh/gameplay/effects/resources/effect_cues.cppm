export module games.generalszh.gameplay.effects.resources.effect_cues;
import std;

export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// The tick's FX lists the game logic plays for the presentation to show (FXList::doFXObj / doFXPos from game rules that
// have no cue of their own), in the order played: by name, on an object (`on`: its position then) or at a spot. Cleared
// as the tick starts; never read by the simulation.
export namespace generalszh::gameplay
{
struct EffectCue
{
	std::string effect;
	Engine::Math::FixedVector3 at;
	ecs::Entity on;
	// `effect` names a particle system started riding on `on` (createParticleSystem, attachToObject), not an FX list.
	bool particleSystem{false};
	// A scorch mark of this radius left at `at` (GameClient::addScorch, SCORCH_1); 0: none. `effect` may then be empty.
	Engine::Math::Fixed scorch;
	// Drawable::fadeIn / fadeOut on `on` over `fadeTicks` (1 in, 2 out; 0: none); `effect` then names the sound played on
	// the source (`at`), none when empty.
	std::uint8_t fade{0};
	std::uint64_t fadeTicks{0};
};

struct EffectCues
{
	std::vector<EffectCue> list;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::EffectCues>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.effect_cues";
};
}
