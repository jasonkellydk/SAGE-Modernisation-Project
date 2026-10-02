export module games.generalszh.gameplay.scripts.algorithms.object_counting;
import std;
import engine.gameplay.common.identity.components.owner;
import engine.ecs.query.query;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.gameplay.scripts.resources.script_records;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.rts.death.components.dying;
import engine.gameplay.common.health.components.inactive_body;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.containment.components.garrison;
import engine.gameplay.common.identity.components.captured;
import engine.gameplay.common.spatial.components.object_shroud;

// The script conditions' object counting: a type parameter names an object type list the scripts made, else one
// type (ScriptConditions::objectTypesFromParam; types that do not exist drop out: prepForPlayerCounting), and a
// player's objects are counted per type (Player::countObjectsByThingTemplate over its teams: an object counts once,
// for the first type it is, a reskin counting as what it was reskinned from and the reverse
// (ThingTemplate::isEquivalentTo); optionally not the effectively dead; not those under construction).
export namespace generalszh::gameplay
{
inline std::vector<std::string> ObjectTypesFrom(const GameWorld &game, const ScriptRecords &records, const std::string &parameter)
{
	std::vector<std::string> types;
	if (parameter.empty())
		return types;
	if (const auto *list = records.List(parameter))
		types = *list;
	else
		types.push_back(parameter);
	const auto &objects = game.templates.Content().objects;
	std::erase_if(types, [&](const std::string &type) { return objects.Find(type) == nullptr; });
	return types;
}

inline bool EquivalentTypes(const GameWorld &game, const std::string &a, const std::string &b)
{
	if (a == b)
		return true;
	const auto &objects = game.templates.Content().objects;
	const auto *first = objects.Find(a);
	const auto *second = objects.Find(b);
	if (first == nullptr || second == nullptr)
		return false;
	return first->reskinnedFrom == b || second->reskinnedFrom == a || (!first->reskinnedFrom.empty() && first->reskinnedFrom == second->reskinnedFrom);
}

// The player's objects of each type (none known: nothing), summed.
inline std::int64_t CountPlayerObjects(const GameWorld &game, std::uint32_t player, const std::vector<std::string> &types, bool ignoreDead,
	bool ignoreUnderConstruction = true)
{
	std::int64_t total = 0;
	if (types.empty())
		return total;
	// Whether a definition is (equivalent to) one of the types, worked out once per definition met: -1 not yet. The
	// types' templates are found once (EquivalentTypes, with each name looked up once).
	std::vector<std::int8_t> matches(game.templates.DefinitionCount(), -1);
	const auto &objects = game.templates.Content().objects;
	std::vector<const content::ObjectDefinition *> typeObjects;
	typeObjects.reserve(types.size());
	for (const std::string &type : types)
		typeObjects.push_back(objects.Find(type));
	const auto equivalent = [&](const std::string &name, const content::ObjectDefinition *object, std::size_t type) {
		if (name == types[type])
			return true;
		const content::ObjectDefinition *other = typeObjects[type];
		if (object == nullptr || other == nullptr)
			return false;
		return object->reskinnedFrom == types[type] || other->reskinnedFrom == name ||
			(!object->reskinnedFrom.empty() && object->reskinnedFrom == other->reskinnedFrom);
	};
	for (std::uint32_t team = 0; team < game.roster.TeamCount(); ++team)
	{
		const auto &record = game.roster.TeamAt(team);
		if (record.owner != player)
			continue;
		for (const ecs::Entity member : record.members)
		{
			if (!game.world.IsAlive(member))
				continue;
			const auto *ref = game.world.Get<engine::gameplay::DefinitionRef>(member);
			if (ref == nullptr || ref->index >= matches.size())
				continue;
			std::int8_t &match = matches[ref->index];
			if (match < 0)
			{
				const std::string &name = game.templates.DefinitionAt(ref->index).name;
				const content::ObjectDefinition *object = game.templates.CatalogEntryOf(ref->index);
				match = 0;
				for (std::size_t type = 0; type < types.size() && match == 0; ++type)
					match = equivalent(name, object, type) ? 1 : 0;
			}
			if (match == 0)
				continue;
			if (ignoreDead && (game.world.Get<engine::gameplay::Dying>(member) != nullptr || game.world.Get<engine::gameplay::InactiveBody>(member) != nullptr))
				continue;
			if (ignoreUnderConstruction && game.world.Get<engine::gameplay::UnderConstruction>(member) != nullptr)
				continue;
			++total;
		}
	}
	return total;
}

// evaluateSkirmishPlayerHasComparisonGarrisoned: the player's objects whose container is a garrison (isGarrisonable)
// with anyone inside. (The original walks the player's teams' members; an object's Owner is its team's player, so a
// chunked pass over the garrisons counts the same.)
inline std::int64_t CountGarrisonedBuildings(const GameWorld &game, std::uint32_t player)
{
	namespace gp = engine::gameplay;
	std::int64_t count = 0;
	ecs::Query<ecs::Read<gp::Garrison>, ecs::Read<gp::Owner>> query(const_cast<ecs::World &>(game.world));
	query.ForEachChunk([&](auto chunk) {
		const auto owners = chunk.template Get<gp::Owner>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < owners.size(); ++row)
			if (owners[row].player == player && game.manifest.Count(entities[row]) > 0)
				++count;
	});
	return count;
}

// evaluateSkirmishPlayerHasComparisonCapturedUnits: the player's captured objects (Object::isCaptured: an unmanned
// vehicle taken over by infantry).
inline std::int64_t CountCapturedObjects(const GameWorld &game, std::uint32_t player)
{
	namespace gp = engine::gameplay;
	std::int64_t count = 0;
	ecs::Query<ecs::Read<gp::Captured>, ecs::Read<gp::Owner>> query(const_cast<ecs::World &>(game.world));
	query.ForEachChunk([&](auto chunk) {
		for (const gp::Owner &owner : chunk.template Get<gp::Owner>())
			count += owner.player == player ? 1 : 0;
	});
	return count;
}

// evaluateSkirmishPlayerHasDiscoveredPlayer: one of the player's objects shown to `by` (its shrouded status clear or
// partly clear; one not tracked is clear).
inline bool DiscoveredBy(const GameWorld &game, std::uint32_t player, std::uint32_t by)
{
	namespace gp = engine::gameplay;
	bool found = false;
	ecs::Query<ecs::Read<gp::Owner>, ecs::Optional<gp::ObjectShroud>> query(const_cast<ecs::World &>(game.world));
	query.ForEachChunk([&](auto chunk) {
		if (found)
			return;
		const auto owners = chunk.template Get<gp::Owner>();
		const auto shrouds = chunk.template Get<gp::ObjectShroud>();
		for (std::size_t row = 0; row < owners.size() && !found; ++row)
			found = owners[row].player == player && (shrouds.empty() || shrouds[row].SeenBy(by));
	});
	return found;
}
}
