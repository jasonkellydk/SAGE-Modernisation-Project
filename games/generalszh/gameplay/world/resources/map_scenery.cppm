export module games.generalszh.gameplay.world.resources.map_scenery;
import std;

export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;
export import engine.core.serialization.byte_stream;
export import engine.gameplay.common.spatial.algorithms.footprint;
export import games.generalszh.content.objects.object_definition;
import engine.ecs.system.system;

// The map's scenery that is never a logic object (GameLogic::startNewGame). Placing the map's objects: shrubbery is left
// out while trees are off (GlobalData's UseTrees); an OPTIMIZED_TREE becomes a tree in the client's tree buffer
// (createOptimizedTree: a drawable made and dropped, no object); a PROP, and a CLEARED_BY_BUILD object with no
// FenceWidth (fluff) while fluff is forced to props, a prop in the client's prop buffer (W3DTerrainVisual::addProp).
// Fluff is forced below High detail (Custom with shadow volumes on is not), and a network game always uses trees and
// forces fluff. The rules are the match's (made once as it starts, checkpointed: a saved game's scenery is its start's).
//
// What the logic clears away for a structure (BuildAssistant::clearRemovableForConstruction ->
// TerrainVisual::removeTreesAndPropsForConstruction) is told to the client as each structure's footprint, kept for the
// match (checkpointed: a loaded game's client clears its scenery again).
export namespace generalszh::gameplay
{
struct MapSceneryRules
{
	bool useTrees{true};
	bool forceFluffToProp{false};

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.Flag(useTrees);
		writer.Flag(forceFluffToProp);
	}
	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto trees = reader.Flag();
		const auto fluff = reader.Flag();
		if (!trees || !fluff)
			return false;
		useTrees = *trees;
		forceFluffToProp = *fluff;
		return true;
	}
};

enum class MapObjectRole : std::uint8_t
{
	Object,  // a logic object
	Skipped, // not made (shrubbery while trees are off)
	Tree,    // a tree in the client's tree buffer
	Prop,    // a prop in the client's prop buffer
};

inline MapObjectRole MapObjectRoleOf(const content::ObjectDefinition &object, const MapSceneryRules &rules)
{
	if (object.Is("SHRUBBERY") && !rules.useTrees)
		return MapObjectRole::Skipped;
	if (object.Is("OPTIMIZED_TREE"))
		return MapObjectRole::Tree;
	const bool fluff = object.Is("CLEARED_BY_BUILD") && object.fenceWidth == Engine::Math::Fixed{};
	if (object.Is("PROP") || (fluff && rules.forceFluffToProp))
		return MapObjectRole::Prop;
	return MapObjectRole::Object;
}

struct SceneryClearing
{
	Engine::Math::FixedVector2 at;
	Engine::Math::TurnAngle facing;
	engine::gameplay::Footprint footprint;
};

struct SceneryClearings
{
	std::vector<SceneryClearing> list;

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(list.size()));
		for (const SceneryClearing &clearing : list)
		{
			writer.I64(clearing.at.x.Raw());
			writer.I64(clearing.at.y.Raw());
			writer.U32(clearing.facing.units);
			writer.U8(static_cast<std::uint8_t>(clearing.footprint.shape));
			writer.I64(clearing.footprint.major.Raw());
			writer.I64(clearing.footprint.minor.Raw());
		}
	}
	bool Load(engine::core::serialization::ByteReader &reader)
	{
		list.clear();
		const auto count = reader.U32();
		if (!count)
			return false;
		for (std::uint32_t index = 0; index < *count; ++index)
		{
			const auto x = reader.I64(), y = reader.I64();
			const auto facing = reader.U32();
			const auto shape = reader.U8();
			const auto major = reader.I64(), minor = reader.I64();
			if (!x || !y || !facing || !shape || !major || !minor || *shape > 1)
				return false;
			list.push_back({{Engine::Math::Fixed::FromRaw(*x), Engine::Math::Fixed::FromRaw(*y)}, Engine::Math::TurnAngle{*facing},
				{static_cast<engine::gameplay::FootprintShape>(*shape), Engine::Math::Fixed::FromRaw(*major), Engine::Math::Fixed::FromRaw(*minor)}});
		}
		return true;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::MapSceneryRules>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.map_scenery_rules";
};
template<>
struct ResourceTraits<generalszh::gameplay::SceneryClearings>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.scenery_clearings";
};
}
