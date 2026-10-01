export module games.generalszh.gameplay.effects.algorithms.bone_fx_effects;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.effects.systems.bone_fx_system;
export import games.generalszh.gameplay.effects.resources.effect_cues;
import games.generalszh.gameplay.creation.algorithms.creation_list_runner;

// The tick's BoneFxEvents, in order: an FX list plays at the bone (FXList::doFXPos: at the spot, on no object) and a
// creation list runs there with the object as its primary (ObjectCreationList::create(ocl, building, &bonePos, nullptr,
// INVALID_ANGLE): its team, facing and veterancy).
export namespace generalszh::gameplay
{
inline void ApplyBoneFx(GameWorld &game)
{
	auto &world = game.world;
	auto *resource = world.FindResource<BoneFxEvents>();
	if (resource == nullptr)
		return;
	std::vector<BoneFxEvent> events;
	resource->AppendTo(events);
	resource->Reset(0);
	auto *cues = world.FindResource<EffectCues>();
	for (const BoneFxEvent &event : events)
	{
		const content::BoneFxContent *config = game.templates.BoneFxOf(event.definition);
		if (config == nullptr)
			continue;
		if (event.kind == BoneFxEvent::Kind::FxList)
		{
			const std::string &name = config->fx[event.state][event.slot].name;
			if (cues != nullptr && !name.empty())
				cues->list.push_back({name, event.position, ecs::Entity{}});
			continue;
		}
		const std::string &list = config->ocl[event.state][event.slot].name;
		if (!list.empty())
			RunCreationList(game, list, {event.position, event.facing, event.team, world.IsAlive(event.source) ? event.source : ecs::Entity{}, event.veterancy});
	}
}
}
