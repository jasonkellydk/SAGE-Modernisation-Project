export module engine.level.adapters.generals_map.map_reader;
import std;

export import engine.level.model.level;
import engine.level.adapters.generals_map.chunks.chunk_file;

// Generals / Zero Hour .map -> engine::level::Level.
//   HeightMapData   -> terrain (10 units per cell, heights byte * 10/16)
//   BlendTileData   -> terrain surface (tiles, blends, cliffs, materials)
//   WorldInfo       -> level properties
//   ObjectsList     -> placements; objects carrying "waypointID" become markers
//   WaypointsList   -> marker links
//   PolygonTriggers -> regions
//   GlobalLighting  -> lighting (4 times of day, 3 terrain + 3 object lights)
//   SidesList       -> scenario: participants (+ AI build plan), groups, scripts
//   anything else   -> raw sections (blend tiles, sides/teams/scripts, ...)
export namespace engine::level::generals_map
{
namespace math = Engine::Math;

struct ReadResult
{
	Level level;
	std::vector<std::string> warnings;
};

// A script file (the original's .scb: SkirmishScripts, MultiplayerScripts): its script lists in file order, the
// player each belongs to by name (ScriptsPlayers, in the same order), and the teams it defines (ScriptTeams).
struct ScriptFile
{
	std::vector<ScriptList> lists;
	std::vector<std::string> players;
	std::vector<Properties> teams;
};

namespace detail
{
using namespace Engine::Math::Literals;

inline void ReadHeightMap(ChunkCursor data, const ChunkHeader &header, Heightfield &terrain)
{
	if (header.version < 2)
		throw FormatError("HeightMapData version 1 (5-unit cells) is not supported");
	const std::int32_t width = data.ReadI32();
	const std::int32_t height = data.ReadI32();
	const std::int32_t border = header.version >= 3 ? data.ReadI32() : 0;
	if (width <= 0 || height <= 0 || border < 0)
		throw FormatError("invalid heightmap dimensions");
	terrain.width = static_cast<std::uint32_t>(width);
	terrain.height = static_cast<std::uint32_t>(height);
	terrain.border = static_cast<std::uint32_t>(border);
	terrain.cellSize = 10_fx;
	if (header.version >= 4)
	{
		const std::int32_t count = data.ReadI32();
		for (std::int32_t index = 0; index < count; ++index)
		{
			const std::int32_t x = data.ReadI32();
			const std::int32_t y = data.ReadI32();
			terrain.playableExtents.push_back({x, y});
		}
	}
	else
	{
		terrain.playableExtents.push_back({width - 2 * border, height - 2 * border});
	}
	const std::int32_t size = data.ReadI32();
	if (static_cast<std::int64_t>(size) != static_cast<std::int64_t>(width) * height)
		throw FormatError("heightmap sample count does not match its dimensions");
	const auto samples = data.ReadBytes(static_cast<std::size_t>(size));
	terrain.heights.reserve(samples.size());
	for (const std::byte sample : samples)
		terrain.heights.push_back(math::Fixed::FromRatio(std::to_integer<std::int64_t>(sample) * 5, 8));
}

inline void ReadObjects(ChunkCursor data, Level &level, std::vector<std::string> &warnings)
{
	// The original loader drops objects far outside the valid height range.
	const math::Fixed minimumZ = -1000_fx;
	const math::Fixed maximumZ = 1593.75_fx;
	while (!data.AtEnd())
	{
		ChunkHeader header;
		ChunkCursor object = data.OpenChunk(header);
		if (header.name != "Object")
		{
			warnings.push_back("ObjectsList: skipped unexpected chunk '" + std::string(header.name) + "'");
			continue;
		}
		math::FixedVector3 position{object.ReadFixed(), object.ReadFixed(), object.ReadFixed()};
		if (header.version <= 2)
			position.z = math::Fixed{};
		const math::TurnAngle orientation = math::TurnFromRadiansBinary32Bits(object.ReadU32());
		const std::uint32_t flags = object.ReadU32();
		std::string type = object.ReadString();
		Properties properties = header.version >= 2 ? object.ReadProperties() : Properties{};
		if (position.z < minimumZ || position.z > maximumZ)
		{
			warnings.push_back("dropped '" + type + "' outside the valid height range");
			continue;
		}
		if (const auto id = properties.Get<std::int64_t>("waypointID"))
		{
			Marker marker;
			marker.id = static_cast<std::uint32_t>(*id);
			marker.name = properties.Get<std::string>("waypointName").value_or("");
			marker.position = position;
			marker.properties = std::move(properties);
			level.markers.push_back(std::move(marker));
			continue;
		}
		level.placements.push_back({std::move(type), position, orientation, flags, std::move(properties)});
	}
}

inline void ReadRegions(ChunkCursor data, const ChunkHeader &header, Level &level)
{
	const std::int32_t count = data.ReadI32();
	for (std::int32_t index = 0; index < count; ++index)
	{
		Region region;
		region.name = data.ReadString();
		if (header.version >= 4)
			region.layer = data.ReadString();
		region.id = data.ReadU32();
		if (header.version >= 2)
			region.water = data.ReadU8() != 0;
		if (header.version >= 3)
		{
			region.river = data.ReadU8() != 0;
			region.riverStart = data.ReadU32();
		}
		const std::int32_t points = data.ReadI32();
		for (std::int32_t point = 0; point < points; ++point)
		{
			const std::int32_t x = data.ReadI32();
			const std::int32_t y = data.ReadI32();
			const std::int32_t z = data.ReadI32();
			region.points.push_back({math::Fixed::FromInt(x), math::Fixed::FromInt(y), math::Fixed::FromInt(z)});
		}
		level.regions.push_back(std::move(region));
	}
}

inline Light ReadLight(ChunkCursor &data)
{
	Light light;
	for (math::Fixed &channel : light.ambient)
		channel = data.ReadFixed();
	for (math::Fixed &channel : light.diffuse)
		channel = data.ReadFixed();
	light.direction = {data.ReadFixed(), data.ReadFixed(), data.ReadFixed()};
	return light;
}

inline void ReadLighting(ChunkCursor data, const ChunkHeader &header, Lighting &lighting)
{
	// Stored time of day is 1-based (morning); unset lights point straight down.
	const std::int32_t timeOfDay = data.ReadI32();
	lighting.current = timeOfDay > 0 ? static_cast<std::uint32_t>(timeOfDay - 1) : 0;
	const Light unset{{}, {}, {math::Fixed{}, math::Fixed{}, -1_fx}};
	lighting.sets.assign(4, LightingSet{std::vector<Light>(3, unset), std::vector<Light>(3, unset)});
	for (LightingSet &set : lighting.sets)
	{
		set.terrain[0] = ReadLight(data);
		set.objects[0] = ReadLight(data);
		if (header.version >= 2)
			for (std::size_t light = 1; light < 3; ++light)
				set.objects[light] = ReadLight(data);
		if (header.version >= 3)
			for (std::size_t light = 1; light < 3; ++light)
				set.terrain[light] = ReadLight(data);
	}
	if (!data.AtEnd())
		lighting.shadowColor = data.ReadU32();
}

inline std::vector<std::uint16_t> ReadU16Array(ChunkCursor &data, std::size_t count)
{
	const auto bytes = data.ReadBytes(count * 2);
	std::vector<std::uint16_t> values(count);
	for (std::size_t index = 0; index < count; ++index)
		values[index] = static_cast<std::uint16_t>(std::to_integer<unsigned>(bytes[index * 2]) | (std::to_integer<unsigned>(bytes[index * 2 + 1]) << 8));
	return values;
}

inline void ReadBlendTiles(ChunkCursor data, const ChunkHeader &header, const Heightfield &terrain, TerrainSurface &surface)
{
	if (terrain.heights.empty())
		throw FormatError("BlendTileData before HeightMapData");
	if (header.version < 2)
		throw FormatError("BlendTileData version 1 is not supported");
	const std::size_t samples = terrain.heights.size();
	if (static_cast<std::size_t>(data.ReadI32()) != samples)
		throw FormatError("blend data size does not match the heightmap");
	surface.tiles = ReadU16Array(data, samples);
	surface.blends = ReadU16Array(data, samples);
	surface.extraBlends = header.version >= 6 ? ReadU16Array(data, samples) : std::vector<std::uint16_t>(samples, 0);
	surface.cliffs = header.version >= 5 ? ReadU16Array(data, samples) : std::vector<std::uint16_t>(samples, 0);
	surface.cliffFlagBytesPerRow = (terrain.width + 7) / 8;
	surface.cliffFlags.assign(static_cast<std::size_t>(surface.cliffFlagBytesPerRow) * terrain.height, 0);
	if (header.version >= 7)
	{
		// Version 7 was saved with a row stride one byte short for some widths.
		const std::uint32_t storedStride = header.version == 7 ? (terrain.width + 1) / 8 : surface.cliffFlagBytesPerRow;
		const auto bytes = data.ReadBytes(static_cast<std::size_t>(storedStride) * terrain.height);
		for (std::uint32_t row = 0; row < terrain.height; ++row)
			for (std::uint32_t column = 0; column < storedStride && column < surface.cliffFlagBytesPerRow; ++column)
				surface.cliffFlags[static_cast<std::size_t>(row) * surface.cliffFlagBytesPerRow + column] =
					std::to_integer<std::uint8_t>(bytes[static_cast<std::size_t>(row) * storedStride + column]);
	}
	surface.tileCount = data.ReadU32();
	const std::int32_t blendCount = data.ReadI32();
	const std::int32_t cliffCount = header.version >= 5 ? data.ReadI32() : 1;
	const std::int32_t materialCount = data.ReadI32();
	// A stored cliff count of 0 is valid: entry 0 ("none") is implicit.
	if (blendCount < 1 || cliffCount < 0 || materialCount < 0)
		throw FormatError("invalid blend table sizes (version " + std::to_string(header.version) + ", blends " +
			std::to_string(blendCount) + ", cliffs " + std::to_string(cliffCount) + ", materials " + std::to_string(materialCount) +
			", heightmap " + std::to_string(terrain.width) + "x" + std::to_string(terrain.height) + ")");
	for (std::int32_t index = 0; index < materialCount; ++index)
	{
		TerrainMaterial material;
		material.firstTile = data.ReadU32();
		material.tileCount = data.ReadU32();
		material.widthInTiles = data.ReadU32();
		data.ReadU32(); // unused legacy field
		material.name = data.ReadString();
		surface.materials.push_back(std::move(material));
	}
	if (header.version >= 4)
	{
		surface.edgeTileCount = data.ReadU32();
		const std::int32_t edgeCount = data.ReadI32();
		for (std::int32_t index = 0; index < edgeCount; ++index)
		{
			TerrainMaterial material;
			material.firstTile = data.ReadU32();
			material.tileCount = data.ReadU32();
			material.widthInTiles = data.ReadU32();
			material.name = data.ReadString();
			surface.edgeMaterials.push_back(std::move(material));
		}
	}
	surface.blendTable.resize(static_cast<std::size_t>(blendCount));
	for (std::int32_t index = 1; index < blendCount; ++index)
	{
		TerrainBlend &blend = surface.blendTable[static_cast<std::size_t>(index)];
		blend.tile = data.ReadU32();
		blend.horizontal = data.ReadU8() != 0;
		blend.vertical = data.ReadU8() != 0;
		blend.rightDiagonal = data.ReadU8() != 0;
		blend.leftDiagonal = data.ReadU8() != 0;
		blend.inverted = data.ReadU8();
		if (header.version >= 3)
			blend.longDiagonal = data.ReadU8() != 0;
		if (header.version >= 4)
			blend.edgeMaterial = data.ReadI32();
		if (data.ReadU32() != 0x7ADA0000u)
			throw FormatError("blend entry " + std::to_string(index) + " is missing its end marker");
	}
	surface.cliffTable.resize(static_cast<std::size_t>(cliffCount > 0 ? cliffCount : 1));
	for (std::int32_t index = 1; index < cliffCount && header.version >= 5; ++index)
	{
		CliffMapping &cliff = surface.cliffTable[static_cast<std::size_t>(index)];
		cliff.tile = data.ReadU32();
		for (math::Fixed &coordinate : cliff.uv)
			coordinate = data.ReadFixed();
		cliff.flip = data.ReadU8() != 0;
		cliff.mutant = data.ReadU8() != 0;
	}
}

// Script parameter type whose value is stored as a position.
constexpr std::uint32_t PositionParameter = 16;

inline ScriptCall ReadCall(ChunkCursor &data, bool hasName)
{
	ScriptCall call;
	call.kind = data.ReadU32();
	if (hasName)
		call.name = std::string(data.NameOf(data.ReadU32() >> 8));
	const std::int32_t count = data.ReadI32();
	if (count < 0 || count > 64)
		throw FormatError("script call has " + std::to_string(count) + " parameters");
	for (std::int32_t index = 0; index < count; ++index)
	{
		ScriptParameter parameter;
		parameter.kind = data.ReadU32();
		if (parameter.kind == PositionParameter)
			parameter.position = {data.ReadFixed(), data.ReadFixed(), data.ReadFixed()};
		else
		{
			parameter.integer = data.ReadI32();
			parameter.number = data.ReadFixed();
			parameter.text = data.ReadString();
		}
		call.parameters.push_back(std::move(parameter));
	}
	return call;
}

inline Script ReadScript(ChunkCursor data, const ChunkHeader &header)
{
	Script script;
	script.name = data.ReadString();
	script.comment = data.ReadString();
	script.conditionComment = data.ReadString();
	script.actionComment = data.ReadString();
	script.active = data.ReadU8() != 0;
	script.oneShot = data.ReadU8() != 0;
	for (bool &level : script.difficulty)
		level = data.ReadU8() != 0;
	script.subroutine = data.ReadU8() != 0;
	if (header.version >= 2)
		script.evaluationDelaySeconds = data.ReadU32();
	while (!data.AtEnd())
	{
		ChunkHeader child;
		ChunkCursor content = data.OpenChunk(child);
		if (child.name == "OrCondition")
		{
			std::vector<ScriptCall> clause;
			while (!content.AtEnd())
			{
				ChunkHeader condition;
				ChunkCursor call = content.OpenChunk(condition);
				if (condition.name != "Condition")
					throw FormatError("unexpected '" + std::string(condition.name) + "' in OrCondition");
				clause.push_back(ReadCall(call, condition.version >= 4));
			}
			script.conditions.push_back(std::move(clause));
		}
		else if (child.name == "ScriptAction")
			script.actions.push_back(ReadCall(content, child.version >= 2));
		else if (child.name == "ScriptActionFalse")
			script.falseActions.push_back(ReadCall(content, child.version >= 2));
		else
			throw FormatError("unexpected '" + std::string(child.name) + "' in Script");
	}
	return script;
}

inline ScriptList ReadScriptList(ChunkCursor data)
{
	ScriptList list;
	while (!data.AtEnd())
	{
		ChunkHeader header;
		ChunkCursor content = data.OpenChunk(header);
		if (header.name == "Script")
			list.scripts.push_back(ReadScript(content, header));
		else if (header.name == "ScriptGroup")
		{
			ScriptGroup group;
			group.name = content.ReadString();
			group.active = content.ReadU8() != 0;
			if (header.version >= 2)
				group.subroutine = content.ReadU8() != 0;
			while (!content.AtEnd())
			{
				ChunkHeader scriptHeader;
				ChunkCursor script = content.OpenChunk(scriptHeader);
				if (scriptHeader.name != "Script")
					throw FormatError("unexpected '" + std::string(scriptHeader.name) + "' in ScriptGroup");
				group.scripts.push_back(ReadScript(script, scriptHeader));
			}
			list.groups.push_back(std::move(group));
		}
		else
			throw FormatError("unexpected '" + std::string(header.name) + "' in ScriptList");
	}
	return list;
}

inline void ReadSides(ChunkCursor data, const ChunkHeader &header, Scenario &scenario)
{
	const std::int32_t sides = data.ReadI32();
	for (std::int32_t side = 0; side < sides; ++side)
	{
		Participant participant;
		participant.properties = data.ReadProperties();
		const std::int32_t entries = data.ReadI32();
		for (std::int32_t entry = 0; entry < entries; ++entry)
		{
			PlannedPlacement planned;
			planned.name = data.ReadString();
			planned.type = data.ReadString();
			planned.position = {data.ReadFixed(), data.ReadFixed(), data.ReadFixed()};
			planned.position.z = math::Fixed{}; // the original forces plans to ground level
			planned.orientation = math::TurnFromRadiansBinary32Bits(data.ReadU32());
			planned.initiallyPlaced = data.ReadU8() != 0;
			planned.rebuilds = data.ReadI32();
			if (header.version >= 3)
			{
				planned.script = data.ReadString();
				planned.health = data.ReadI32();
				planned.reportsWhenAttacked = data.ReadU8() != 0;
				planned.sellable = data.ReadU8() == 0;
				planned.repairable = data.ReadU8() != 0;
			}
			participant.plan.push_back(std::move(planned));
		}
		scenario.participants.push_back(std::move(participant));
	}
	if (header.version >= 2)
	{
		const std::int32_t teams = data.ReadI32();
		for (std::int32_t team = 0; team < teams; ++team)
			scenario.groups.push_back(data.ReadProperties());
	}
	// Script lists follow, one per participant, in participant order.
	std::size_t next = 0;
	while (!data.AtEnd())
	{
		ChunkHeader child;
		ChunkCursor content = data.OpenChunk(child);
		if (child.name != "PlayerScriptsList")
			throw FormatError("unexpected '" + std::string(child.name) + "' in SidesList");
		while (!content.AtEnd())
		{
			ChunkHeader listHeader;
			ChunkCursor list = content.OpenChunk(listHeader);
			if (listHeader.name != "ScriptList")
				throw FormatError("unexpected '" + std::string(listHeader.name) + "' in PlayerScriptsList");
			ScriptList scripts = ReadScriptList(list);
			if (next < scenario.participants.size())
				scenario.participants[next].scripts = std::move(scripts);
			++next;
		}
	}
}

inline void LinkMarkers(ChunkCursor data, Level &level, std::vector<std::string> &warnings)
{
	std::map<std::uint32_t, Marker *> byId;
	for (Marker &marker : level.markers)
		byId[marker.id] = &marker;
	const std::int32_t count = data.ReadI32();
	for (std::int32_t index = 0; index < count; ++index)
	{
		const std::uint32_t from = data.ReadU32();
		const std::uint32_t to = data.ReadU32();
		const auto source = byId.find(from);
		if (source == byId.end() || !byId.contains(to))
		{
			warnings.push_back("waypoint link " + std::to_string(from) + " -> " + std::to_string(to) + " refers to a missing waypoint");
			continue;
		}
		source->second->links.push_back(to);
	}
}
}

inline std::expected<ReadResult, std::string> Read(std::span<const std::byte> bytes)
{
	auto file = ChunkFile::Parse(bytes);
	if (!file)
		return std::unexpected(file.error());
	ReadResult result;
	Level &level = result.level;
	std::string current = "file";
	try
	{
		// Links refer to markers, which may come later in the file.
		std::vector<ChunkCursor> pendingLinks;
		ChunkCursor body = file->Body();
		while (!body.AtEnd())
		{
			ChunkHeader header;
			ChunkCursor data = body.OpenChunk(header);
			current = std::string(header.name);
			if (header.name == "HeightMapData")
				detail::ReadHeightMap(data, header, level.terrain);
			else if (header.name == "WorldInfo")
				level.properties = data.ReadProperties();
			else if (header.name == "ObjectsList")
				detail::ReadObjects(data, level, result.warnings);
			else if (header.name == "WaypointsList")
				pendingLinks.push_back(data);
			else if (header.name == "PolygonTriggers")
				detail::ReadRegions(data, header, level);
			else if (header.name == "GlobalLighting")
				detail::ReadLighting(data, header, level.lighting);
			else if (header.name == "BlendTileData")
				detail::ReadBlendTiles(data, header, level.terrain, level.surface);
			else if (header.name == "SidesList")
				detail::ReadSides(data, header, level.scenario);
			else
			{
				const auto raw = data.ReadBytes(data.Remaining());
				level.sections.push_back({std::string(header.name), header.version, {raw.begin(), raw.end()}});
			}
		}
		current = "WaypointsList";
		for (ChunkCursor &links : pendingLinks)
			detail::LinkMarkers(links, level, result.warnings);
	}
	catch (const FormatError &error)
	{
		return std::unexpected(current + ": " + error.what());
	}
	if (level.terrain.heights.empty())
		return std::unexpected("map has no HeightMapData");
	return result;
}

// A .scb script file (DataChunkInput with PlayerScriptsList, ScriptsPlayers and ScriptTeams parsers; the rest of the
// file, objects, triggers and waypoints, is not read by the original either).
inline std::expected<ScriptFile, std::string> ReadScriptFile(std::span<const std::byte> bytes)
{
	auto file = ChunkFile::Parse(bytes);
	if (!file)
		return std::unexpected(file.error());
	ScriptFile result;
	std::string current = "file";
	try
	{
		ChunkCursor body = file->Body();
		while (!body.AtEnd())
		{
			ChunkHeader header;
			ChunkCursor data = body.OpenChunk(header);
			current = std::string(header.name);
			if (header.name == "PlayerScriptsList")
			{
				while (!data.AtEnd())
				{
					ChunkHeader listHeader;
					ChunkCursor list = data.OpenChunk(listHeader);
					if (listHeader.name != "ScriptList")
						throw FormatError("unexpected '" + std::string(listHeader.name) + "' in PlayerScriptsList");
					result.lists.push_back(detail::ReadScriptList(list));
				}
			}
			else if (header.name == "ScriptsPlayers")
			{
				// ParsePlayersDataChunk: version 2 says whether each name has its side's dictionary after it.
				const bool dictionaries = header.version >= 2 && data.ReadI32() != 0;
				const std::int32_t names = data.ReadI32();
				for (std::int32_t index = 0; index < names; ++index)
				{
					result.players.push_back(data.ReadString());
					if (dictionaries)
						(void)data.ReadProperties();
				}
			}
			else if (header.name == "ScriptTeams")
			{
				while (!data.AtEnd())
					result.teams.push_back(data.ReadProperties());
			}
		}
	}
	catch (const FormatError &error)
	{
		return std::unexpected(current + ": " + error.what());
	}
	return result;
}
}
