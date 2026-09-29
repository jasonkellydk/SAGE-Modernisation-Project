export module games.generalszh.gameplay.containment.algorithms.parachuting;
import std;
import engine.gameplay.common.spatial.components.carried;

export import games.generalszh.gameplay.world.resources.game_world;
import engine.gameplay.rts.parachute.algorithms.parachute_rigging;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.rts.movement.components.move_order;
import Engine.Core.Math.FixedAngle;

// Zero Hour's parachutes as objects (AmericaParachute with its ParachuteContain): made with a random sway, and a
// rider put in one (ParachuteContain::onContaining): held, hanging at its harness (PARA_MAN at rest in its FREEFALL
// and PARACHUTING models, else the top of its geometry), facing as the chute.
export namespace generalszh::gameplay
{
namespace gameplay = engine::gameplay;

// ParachuteContain's constructor: its first sway rates, each random up to its most.
void StartParachute(GameWorld &game, ecs::Entity chute, std::uint32_t definition)
{
	const gameplay::ParachuteDefinition &kind = game.world.Resource<gameplay::ParachuteCatalog>().At(definition);
	const auto around = [&](std::int32_t most) {
		const std::int64_t magnitude = most < 0 ? -static_cast<std::int64_t>(most) : most;
		return static_cast<std::int32_t>(Engine::Math::UniformInt(game.random, -magnitude, magnitude));
	};
	gameplay::Parachute state;
	state.definition = definition;
	state.pitchRate = around(kind.pitchRateMax);
	state.rollRate = around(kind.rollRateMax);
	game.world.Add<gameplay::Parachute>(chute);
	*game.world.Get<gameplay::Parachute>(chute) = state;
}

// ParachuteContain::onContaining and positionRider. False when `chute` is no parachute or already holds someone.
bool PutInParachute(GameWorld &game, ecs::Entity chute, ecs::Entity rider)
{
	auto &world = game.world;
	gameplay::Parachute *state = world.IsAlive(chute) ? world.Get<gameplay::Parachute>(chute) : nullptr;
	if (state == nullptr || world.IsAlive(state->rider) || !world.IsAlive(rider))
		return false;
	std::array<Engine::Math::FixedVector3, 2> bones{};
	if (const auto *definition = world.Get<gameplay::DefinitionRef>(rider))
	{
		const auto &riders = game.templates.Content().parachuteRiders;
		if (const auto found = riders.find(game.templates.DefinitionAt(definition->index).name); found != riders.end())
			bones = found->second;
	}
	state->rider = rider;
	state->riderBone = bones[0];
	state->riderOpenBone = bones[1];
	const gameplay::Parachute held = *state;
	const gameplay::Transform at = *world.Get<gameplay::Transform>(chute);
	if (!world.Has<gameplay::ParachuteRider>(rider))
		world.Add<gameplay::ParachuteRider>(rider);
	world.Get<gameplay::ParachuteRider>(rider)->chute = chute;
	// Placed by its chute, not moving itself.
	if (!world.Has<gameplay::Carried>(rider))
		world.Add<gameplay::Carried>(rider);
	world.Get<gameplay::Carried>(rider)->carrier = chute;
	if (!world.Has<gameplay::Disabled>(rider))
		world.Add<gameplay::Disabled>(rider);
	world.Get<gameplay::Disabled>(rider)->mask |= gameplay::disabled_type::Held;
	if (auto *move = world.Get<gameplay::MoveOrder>(rider))
		*move = {};
	const gameplay::ParachuteOffsets offsets = gameplay::RigParachute(world.Resource<gameplay::ParachuteCatalog>().At(held.definition), held, at.facing);
	gameplay::Transform hanging{at.position + offsets.riderAttach, at.facing};
	const Engine::Math::Fixed floor = game.ground.At(hanging.position.XY());
	if (hanging.position.z < floor)
		hanging.position.z = floor;
	*world.Get<gameplay::Transform>(rider) = hanging;
	return true;
}
}
