export module games.generalszh.gameplay.powers.algorithms.spy_visions;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.powers.components.spy_vision;
import games.generalszh.content.powers.special_powers;
import games.generalszh.content.objects.kind_of;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.rts.vision.components.vision;
import engine.gameplay.rts.vision.resources.shroud_map;

// Spy vision outside its system (SpyVisionSystem steps the modules):
// - SetUnitsVisionSpied (Player::setUnitsVisionSpied): each thing of `player`'s of the kinds (none named: everything),
//   in its teams' order, one more (or fewer) spy through its eyes for `byWhom` (Object::setVisionSpied); where that
//   player's bit changes it looks again (handlePartitionCellMaintenance: here its next look). Only the things there as
//   it happens: those made later are not spied on, as the original;
// - ApplySpyVisions (doActivationWork, after the step): each turning on or off, for every player the spying player
//   considers an enemy.
export namespace generalszh::gameplay
{
inline void SetUnitsVisionSpied(GameWorld &game, std::uint32_t player, const std::vector<std::size_t> &kinds, bool setting, std::uint32_t byWhom)
{
	namespace gp = engine::gameplay;
	auto &world = game.world;
	auto *map = world.FindResource<gp::ShroudMap>();
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
	{
		if (game.roster.TeamAt(team).owner != player)
			continue;
		for (const ecs::Entity unit : game.roster.TeamAt(team).members)
		{
			const auto *ref = world.IsAlive(unit) ? world.Get<gp::DefinitionRef>(unit) : nullptr;
			gp::Vision *vision = ref != nullptr ? world.Get<gp::Vision>(unit) : nullptr;
			if (vision == nullptr)
				continue;
			const content::ObjectDefinition &object = game.templates.DefinitionAt(ref->index);
			if (!kinds.empty() && std::none_of(kinds.begin(), kinds.end(), [&](std::size_t bit) { return bit < content::KindOfNames.size() && content::HasKindOf(object.kinds, bit); }))
				continue;
			if (!world.Has<gp::VisionSpies>(unit))
			{
				world.Add<gp::VisionSpies>(unit);
				vision = world.Get<gp::Vision>(unit); // the add moved its components
			}
			if (gp::SetVisionSpied(*world.Get<gp::VisionSpies>(unit), *vision, setting, byWhom) && map != nullptr)
				if (gp::Looker *looker = map->LookerAt(vision->slot); looker != nullptr && looker->entity == unit)
					looker->keyCellX = looker->keyCellY = gp::Looker::Relook;
		}
	}
}

inline void ApplySpyVisions(GameWorld &game)
{
	namespace gp = engine::gameplay;
	auto *resource = game.world.FindResource<SpyVisionEvents>();
	const auto *relationships = game.world.FindResource<gp::Relationships>();
	if (resource == nullptr || relationships == nullptr)
		return;
	std::vector<SpyVisionEvent> events;
	resource->AppendTo(events);
	resource->Reset(0);
	for (const SpyVisionEvent &event : events)
	{
		const auto modules = content::ReadSpyVisions(game.templates.DefinitionAt(event.definition), game.step);
		if (event.module >= modules.size())
			continue;
		std::vector<std::size_t> kinds;
		for (const std::string &kind : modules[event.module].kinds)
			if (!(kind == "NONE" || kind.empty()))
				kinds.push_back(content::KindOfBit(kind));
		for (std::uint32_t other = 0; other < game.roster.PlayerCount(); ++other)
			if (relationships->Enemies(event.player, other))
				SetUnitsVisionSpied(game, other, kinds, event.setting != 0, event.player);
	}
}
}
