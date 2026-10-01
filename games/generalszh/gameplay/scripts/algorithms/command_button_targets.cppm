export module games.generalszh.gameplay.scripts.algorithms.command_button_targets;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
import games.generalszh.gameplay.orders.algorithms.command_buttons;
import games.generalszh.gameplay.orders.algorithms.command_availability;
import games.generalszh.gameplay.teams.algorithms.team_states;
import games.generalszh.gameplay.teams.algorithms.team_actions;
import games.generalszh.gameplay.scripts.algorithms.object_counting;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.components.off_map;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.rts.containment.components.garrison;
import engine.ecs.query.query;
import games.generalszh.gameplay.orders.resources.command_bar_overrides;

// The scripts' command buttons used at objects (ScriptActions): a named unit's own buttons of that name
// (doNamedUseCommandButtonAbility / ...OnNamed: each button of its command set so named, in slot order), a team's
// members all at once (doTeamUseCommandButtonAbilityOnNamed: AIGroup::groupDoCommandButtonAtObject), and a team's
// members at a target the button may be used on (doTeamUseCommandButtonOnNamed / OnNearest...: the button's source
// member must find it valid, CommandButton::isValidToUseOn).
export namespace generalszh::gameplay
{
// Which nearest target a doTeamUseCommandButtonOnNearest... looks for.
enum class ButtonTarget : std::uint8_t
{
	Enemy,                // OnNearestEnemy: anything of an enemy's
	GarrisonableBuilding, // OnNearestGarrisonedBuilding: an enemy's STRUCTURE that may be garrisoned
	KindOf,               // OnNearestKindof: an enemy's of the kind
	Building,             // OnNearestBuilding: an enemy's STRUCTURE
	BuildingClass,        // OnNearestBuildingClass: an enemy's STRUCTURE of the kind
	ObjectType,           // OnNearestObjectType: an enemy's or a neutral's of the type (or of any type of the list)
};

namespace button_target_detail
{
namespace gp = engine::gameplay;

// Team::getTeamAsAIGroup: the team's members, newest first, that an AIGroup takes (AIGroup::add: those with an AI,
// structures and ALWAYS_SELECTABLE things).
inline std::vector<ecs::Entity> GroupOf(GameWorld &game, std::uint32_t team)
{
	std::vector<ecs::Entity> group;
	const auto &members = game.roster.TeamAt(team).members;
	for (auto it = members.rbegin(); it != members.rend(); ++it)
	{
		if (!game.world.IsAlive(*it))
			continue;
		const auto *ref = game.world.Get<gp::DefinitionRef>(*it);
		const bool kept = HasAi(game, *it) ||
			(ref != nullptr && (game.templates.DefinitionAt(ref->index).Is("STRUCTURE") || game.templates.DefinitionAt(ref->index).Is("ALWAYS_SELECTABLE")));
		if (kept)
			group.push_back(*it);
	}
	return group;
}

// AIGroup::getCenter: the members with an AI, not held (riders), averaged; with none of those, all not held. None at
// all: no centre (the original divided by nothing).
inline std::optional<Engine::Math::FixedVector2> CenterOf(GameWorld &game, std::span<const ecs::Entity> group)
{
	const auto held = [&](ecs::Entity entity) {
		const auto *off = game.world.Get<gp::Disabled>(entity);
		return off != nullptr && (off->mask & gp::disabled_type::Held) != 0;
	};
	for (const bool aiOnly : {true, false})
	{
		Engine::Math::FixedVector2 sum;
		std::int64_t count = 0;
		for (const ecs::Entity member : group)
		{
			if (held(member) || (aiOnly && !HasAi(game, member)))
				continue;
			if (const auto *at = game.world.Get<gp::Transform>(member))
			{
				sum += at->position.XY();
				++count;
			}
		}
		if (count > 0)
			return Engine::Math::FixedVector2{sum.x / Engine::Math::Fixed::FromInt(count), sum.y / Engine::Math::Fixed::FromInt(count)};
	}
	return std::nullopt;
}

inline void GroupDoAtObject(GameWorld &game, std::span<const ecs::Entity> group, const content::CommandButtonContent &button, ecs::Entity target)
{
	for (const ecs::Entity member : group)
		DoCommandButtonAtObject(game, member, button, target);
}
}

// doNamedUseCommandButtonAbility / doNamedUseCommandButtonAbilityOnNamed: each button of the unit's command set named
// `ability`, in slot order, used by it (at the target when there is one).
inline void NamedUseCommandButton(GameWorld &game, ecs::Entity unit, const std::string &ability, std::optional<ecs::Entity> target)
{
	if (!game.world.IsAlive(unit) || (target && !game.world.IsAlive(*target)))
		return;
	const content::GameContent &content = game.templates.Content();
	const auto effective = EffectiveCommandSet(content.commands, game.world.FindResource<CommandBarOverrides>(), CommandSetOf(game, unit));
	const auto *set = effective ? &*effective : nullptr;
	if (set == nullptr)
		return;
	for (const std::string &name : set->buttons)
	{
		if (name.empty() || name != ability)
			continue;
		const auto *button = content.commands.Button(name);
		if (button == nullptr)
			continue;
		if (target)
			DoCommandButtonAtObject(game, unit, *button, *target);
		else
			DoCommandButton(game, unit, *button);
	}
}

// doTeamUseCommandButtonAbilityOnNamed: every member uses the button at the target, valid or not.
inline void TeamUseCommandButtonAtObject(GameWorld &game, const std::string &team, const std::string &ability, ecs::Entity target)
{
	const auto index = ResolveTeam(game, team);
	const auto *button = game.templates.Content().commands.Button(ability);
	if (!index || button == nullptr || !game.world.IsAlive(target))
		return;
	button_target_detail::GroupDoAtObject(game, button_target_detail::GroupOf(game, *index), *button, target);
}

// doTeamUseCommandButtonOnNamed: once the member the button is for (getSpecialPowerSourceObject /
// getCommandButtonSourceObject) may use it on the target, every member does.
inline void TeamAllUseCommandButtonOnNamed(GameWorld &game, const std::string &team, const std::string &ability, ecs::Entity target)
{
	const auto index = ResolveTeam(game, team);
	const auto *button = game.templates.Content().commands.Button(ability);
	if (!index || button == nullptr)
		return;
	const std::vector<ecs::Entity> group = button_target_detail::GroupOf(game, *index);
	const ecs::Entity source = ButtonSourceIn(game, group, *button);
	if (source == ecs::Entity{} || !game.world.IsAlive(target))
		return;
	if (ButtonValidOnObject(game, source, *button, target))
		button_target_detail::GroupDoAtObject(game, group, *button, target);
}

// doTeamUseCommandButtonOnNearest...: nearest the group's centre (getClosestObject, FROM_CENTER_2D), of the objects on
// the map as the source member is, those the team's player counts enemies (PartitionFilterPlayerAffiliation
// ALLOW_ENEMIES; its own pass too, as the original's filter lets them) of the kind asked for that the source member may
// use the button on; every member uses it there. `kind`: the KINDOF name for KindOf and BuildingClass; `type`: the object
// type (or list) for ObjectType, which also takes neutrals and is nearest per type.
inline void TeamAllUseCommandButtonOnNearest(GameWorld &game, const std::string &team, const std::string &ability, ButtonTarget target,
	const std::string &kind = {}, const std::string &type = {})
{
	namespace gp = engine::gameplay;
	const auto index = ResolveTeam(game, team);
	const auto *button = game.templates.Content().commands.Button(ability);
	const auto *relationships = game.world.FindResource<gp::Relationships>();
	if (!index || button == nullptr || relationships == nullptr)
		return;
	const std::vector<ecs::Entity> group = button_target_detail::GroupOf(game, *index);
	const ecs::Entity source = ButtonSourceIn(game, group, *button);
	if (source == ecs::Entity{})
		return;
	const auto center = button_target_detail::CenterOf(game, group);
	if (!center)
		return;
	const std::uint32_t player = game.roster.TeamAt(*index).owner;
	const bool sourceOffMap = game.world.Get<gp::OffMap>(source) != nullptr;
	std::vector<std::string> types;
	if (target == ButtonTarget::ObjectType)
	{
		if (game.templates.Content().objects.Find(type) != nullptr)
			types = {type};
		else if (const auto *records = game.world.FindResource<ScriptRecords>())
			types = ObjectTypesFrom(game, *records, type);
		if (types.empty())
			return;
	}
	struct Candidate
	{
		ecs::Entity entity;
		Engine::Math::Fixed distance;
	};
	std::vector<Candidate> candidates;
	ecs::Query<ecs::Read<gp::DefinitionRef>, ecs::Read<gp::Owner>, ecs::Read<gp::Transform>> query(game.world);
	query.ForEachChunk([&](auto chunk) {
		const auto refs = chunk.template Get<gp::DefinitionRef>();
		const auto owners = chunk.template Get<gp::Owner>();
		const auto transforms = chunk.template Get<gp::Transform>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < refs.size(); ++row)
		{
			const std::uint32_t theirs = owners[row].player;
			const bool enemy = theirs == player || relationships->Enemies(player, theirs);
			const bool neutral = !enemy && !relationships->Allies(player, theirs);
			if (!(enemy || (target == ButtonTarget::ObjectType && neutral)))
				continue;
			if ((game.world.Get<gp::OffMap>(entities[row]) != nullptr) != sourceOffMap)
				continue;
			const content::ObjectDefinition &definition = game.templates.DefinitionAt(refs[row].index);
			bool kept = true;
			switch (target)
			{
			case ButtonTarget::Enemy: break;
			case ButtonTarget::GarrisonableBuilding: kept = definition.Is("STRUCTURE") && game.world.Has<gp::Garrison>(entities[row]); break;
			case ButtonTarget::KindOf: kept = definition.Is(kind); break;
			case ButtonTarget::Building: kept = definition.Is("STRUCTURE"); break;
			case ButtonTarget::BuildingClass: kept = definition.Is("STRUCTURE") && definition.Is(kind); break;
			case ButtonTarget::ObjectType:
				kept = std::ranges::any_of(types, [&](const std::string &name) { return EquivalentTypes(game, name, definition.name); });
				break;
			}
			if (kept)
				candidates.push_back({entities[row], Engine::Math::DistanceSquared(transforms[row].position.XY(), *center)});
		}
	});
	// Nearest first (ties in entity order): the first valid one is the nearest valid one, and the costly validity test
	// stops there.
	std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.distance < b.distance; });
	ecs::Entity best;
	Engine::Math::Fixed bestDistance;
	const auto consider = [&](const Candidate &candidate) {
		if ((best == ecs::Entity{} || candidate.distance < bestDistance) && ButtonValidOnObject(game, source, *button, candidate.entity))
		{
			best = candidate.entity;
			bestDistance = candidate.distance;
		}
	};
	if (target == ButtonTarget::ObjectType)
	{
		// Nearest of each type in list order; a later type's must be strictly nearer.
		for (const std::string &name : types)
		{
			ecs::Entity nearest;
			Engine::Math::Fixed nearestDistance;
			for (const Candidate &candidate : candidates)
			{
				const auto *ref = game.world.Get<gp::DefinitionRef>(candidate.entity);
				if (!EquivalentTypes(game, name, game.templates.DefinitionAt(ref->index).name))
					continue;
				if (ButtonValidOnObject(game, source, *button, candidate.entity))
				{
					nearest = candidate.entity;
					nearestDistance = candidate.distance;
					break;
				}
			}
			if (nearest != ecs::Entity{} && (best == ecs::Entity{} || nearestDistance < bestDistance))
			{
				best = nearest;
				bestDistance = nearestDistance;
			}
		}
	}
	else
		for (const Candidate &candidate : candidates)
		{
			consider(candidate);
			if (best != ecs::Entity{})
				break;
		}
	if (best != ecs::Entity{})
		button_target_detail::GroupDoAtObject(game, group, *button, best);
}

// doSkirmishCommandButtonOnMostValuable(team, button, range, all members: unused): of the objects within `range` of the
// group's centre (FROM_CENTER_2D), on the map as the source member is, that the team's player counts enemies (its own
// too, as the filter lets them) and the source member may use the button on, the most expensive to build
// (ITER_SORTED_EXPENSIVE_TO_CHEAP: the template's BuildCost, a stable sort; ties in entity order): every member uses it
// there (groupDoCommandButtonAtObject).
inline void TeamUseCommandButtonOnMostValuable(GameWorld &game, const std::string &team, const std::string &ability, Engine::Math::Fixed range)
{
	namespace gp = engine::gameplay;
	const auto index = ResolveTeam(game, team);
	const auto *button = game.templates.Content().commands.Button(ability);
	const auto *relationships = game.world.FindResource<gp::Relationships>();
	if (!index || button == nullptr || relationships == nullptr)
		return;
	const std::vector<ecs::Entity> group = button_target_detail::GroupOf(game, *index);
	const ecs::Entity source = ButtonSourceIn(game, group, *button);
	if (source == ecs::Entity{})
		return;
	const auto center = button_target_detail::CenterOf(game, group);
	if (!center)
		return;
	const std::uint32_t player = game.roster.TeamAt(*index).owner;
	const bool sourceOffMap = game.world.Get<gp::OffMap>(source) != nullptr;
	const Engine::Math::Fixed reach = range * range;
	struct Candidate
	{
		ecs::Entity entity;
		std::int32_t cost;
	};
	std::vector<Candidate> candidates;
	ecs::Query<ecs::Read<gp::DefinitionRef>, ecs::Read<gp::Owner>, ecs::Read<gp::Transform>> query(game.world);
	query.ForEachChunk([&](auto chunk) {
		const auto refs = chunk.template Get<gp::DefinitionRef>();
		const auto owners = chunk.template Get<gp::Owner>();
		const auto transforms = chunk.template Get<gp::Transform>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < refs.size(); ++row)
		{
			const std::uint32_t theirs = owners[row].player;
			if (theirs != player && !relationships->Enemies(player, theirs))
				continue;
			if ((game.world.Get<gp::OffMap>(entities[row]) != nullptr) != sourceOffMap)
				continue;
			if (Engine::Math::DistanceSquared(transforms[row].position.XY(), *center) > reach)
				continue;
			candidates.push_back({entities[row], game.templates.DefinitionAt(refs[row].index).buildCost});
		}
	});
	// Most expensive first: the first valid one is the iterator's first, and the costly validity test stops there.
	std::stable_sort(candidates.begin(), candidates.end(), [](const Candidate &a, const Candidate &b) { return a.cost > b.cost; });
	for (const Candidate &candidate : candidates)
		if (ButtonValidOnObject(game, source, *button, candidate.entity))
		{
			button_target_detail::GroupDoAtObject(game, group, *button, candidate.entity);
			return;
		}
}
}
