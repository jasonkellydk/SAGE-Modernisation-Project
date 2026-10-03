export module games.generalszh.presentation.interaction.algorithms.force_select;
import std;

export import engine.ecs.core.world;
export import engine.gameplay.common.identity.components.object_id;

// ScriptActions::doForceObjectSelection (OBJECT_FORCE_SELECT): of the team's members whose thing is `type`, the one with
// the lowest object ID (the original's "lower ID means its newer", though it is the oldest it keeps) is what the local
// player gets selected (deselectAllDrawables, selectDrawable), the sound played as the local player's, and with
// centreInView the view moved onto it at once (moveCameraTo, no time). None: nothing at all.
export namespace generalszh::presentation
{
inline std::optional<ecs::Entity> ForceSelectPick(const ecs::World &world, std::span<const ecs::Entity> members, std::string_view type,
	const std::function<std::string_view(ecs::Entity)> &typeOf)
{
	std::optional<ecs::Entity> best;
	std::uint32_t bestId = 0;
	for (const ecs::Entity member : members)
	{
		if (!world.IsAlive(member) || typeOf(member) != type)
			continue;
		const auto *id = world.Get<engine::gameplay::ObjectId>(member);
		const std::uint32_t value = id != nullptr ? id->value : 0u;
		if (!best || value < bestId)
		{
			best = member;
			bestId = value;
		}
	}
	return best;
}
}
