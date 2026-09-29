export module engine.gameplay.rts.movement.resources.movement_penalty;
import std;

export import Engine.Core.Math.Fixed;
export import engine.gameplay.common.health.components.health;
import engine.ecs.system.system;

// When a hurt body slows down (GameData MovementPenaltyDamageState as a share of its maximum health: at or below it,
// its damage state is no better than the penalty state; ActiveBody::calcDamageState). `always`: the penalty state is
// PRISTINE, so every body moves on its damaged rates.
export namespace engine::gameplay
{
struct MovementPenalty
{
	Engine::Math::Fixed healthRatio;
	bool always{false};

	bool Applies(const Health &health) const noexcept
	{
		return always || health.current <= health.maximum * healthRatio;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::MovementPenalty>
{
	static constexpr std::string_view StableName = "engine.gameplay.movement_penalty";
};
}
