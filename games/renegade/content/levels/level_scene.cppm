export module games.renegade.content.levels.level_scene;
import std;
export import games.renegade.content.levels.definition_catalog;
export import games.renegade.content.levels.physics_placement;
export import games.renegade.content.levels.dynamic_level;
export import games.renegade.content.levels.navigation_paths;
export import games.renegade.content.levels.traversal_portals;
import engine.filesystem.core.virtual_file_system;
export import engine.level.model.level;

export namespace renegade::content
{
struct SpawnerPlacement
{
	std::uint32_t id{}, definition{};
	Engine::Math::FixedAffineTransform3 transform;
	bool enabled{};
};
struct LevelScene
{
	// Retail envelopes are a content adapter. Consumers use the same shared
	// authored level model as other games, including full 3D poses/bindings.
	engine::level::Level level;
	std::string static_file, start_script;
	bool first_load{};
	std::vector<PhysicsPlacement> statics;
	std::vector<SpawnerPlacement> spawners;
	DynamicLevel dynamic;
	// Full authored records remain available to subsequent schema adapters.
	std::vector<std::byte> static_data, dynamic_data;
};
struct PlayerStart
{
	std::uint32_t spawner{}, definition{}, physics{};
	std::string model;
	Engine::Math::FixedAffineTransform3 transform;
};

inline std::expected<LevelScene, std::string> ReadLevelScene(persist::Bytes static_bytes, persist::Bytes dynamic_bytes)
{
	using namespace persist;
	LevelScene result;
	const auto info = One(dynamic_bytes, 0x3c51c460);
	if (!info) return std::unexpected(info.error());
	const auto info_fields = Micros(info->payload);
	if (!info_fields) return std::unexpected(info_fields.error());
	for (const auto &field : *info_fields) if (field.id == 1) {
		const auto value = Text(field.payload); if (!value) return std::unexpected(value.error()); result.static_file = *value;
	}
	if (result.static_file.empty()) return std::unexpected("level INFO has no static map filename");
	const auto static_objects = One(static_bytes, 0x20001);
	if (!static_objects) return std::unexpected(static_objects.error());
	const auto scene = One(static_objects->payload, 0x06090609);
	if (!scene) return std::unexpected(scene.error());
	const auto objects = One(scene->payload, 0x00770100);
	if (!objects) return std::unexpected(objects.error());
	const auto placements = Children(objects->payload);
	if (!placements) return std::unexpected(placements.error());
	std::set<std::uint32_t> tokens, instances;
	for (std::size_t i = 0; i < placements->size(); i += 2) {
		if (i + 1 >= placements->size() || (*placements)[i].id != 0x00770101 || (*placements)[i + 1].id != 0x00770102)
			return std::unexpected("static physical object must precede its AAB linkage");
		const auto factories = Children((*placements)[i].payload);
		if (!factories || factories->size() != 1) return std::unexpected("invalid static physical factory wrapper");
		auto placement = ReadPhysicsPlacement(factories->front());
		if (!placement) return std::unexpected(placement.error());
		if (!placement->token || !tokens.insert(placement->token).second || !instances.insert(placement->instance).second)
			return std::unexpected("duplicate or null static physical identity");
		result.statics.push_back(std::move(*placement));
		const auto &source = result.statics.back();
		engine::level::Placement shared;
		shared.id = 0x200000000ull + source.instance;
		shared.definition = source.definition; shared.model = source.render_factory == 0x10000 ? source.model : std::string{};
		shared.transform = source.transform; shared.flags = source.flags; shared.kind = engine::level::PlacementKind::Geometry;
		shared.position = {source.transform.elements[3], source.transform.elements[7], source.transform.elements[11]};
		result.level.placements.push_back(std::move(shared));
	}
	const auto level = One(dynamic_bytes, 0x3c51c461);
	if (!level) return std::unexpected(level.error());
	const auto combat = One(level->payload, 0x40000);
	if (!combat) return std::unexpected(combat.error());
	const auto manager = One(combat->payload, 0x36a82ea7);
	if (!manager) return std::unexpected(manager.error());
	const auto variables = One(manager->payload, 0x36a82ee3);
	if (!variables) return std::unexpected(variables.error());
	const auto state = Micros(variables->payload);
	if (!state) return std::unexpected(state.error());
	for (const auto &field : *state) {
		if (field.id == 1) { const auto value = Flag(field.payload); if (!value) return std::unexpected(value.error()); result.first_load = *value; }
		if (field.id == 4) { const auto value = Text(field.payload); if (!value) return std::unexpected(value.error()); result.start_script = *value; }
	}
	const auto spawn_manager = One(combat->payload, 0x36a82ea9);
	if (!spawn_manager) return std::unexpected(spawn_manager.error());
	const auto spawners = Children(spawn_manager->payload);
	if (!spawners) return std::unexpected(spawners.error());
	std::set<std::uint32_t> spawn_ids;
	for (const auto &record : *spawners) if (record.id == 1014991133) {
		const auto vars = One(record.payload, 1014991054);
		if (!vars) return std::unexpected(vars.error());
		const auto fields = Micros(vars->payload);
		if (!fields) return std::unexpected(fields.error());
		SpawnerPlacement spawn; bool has_transform{}, has_definition{}, has_id{};
		for (const auto &field : *fields) {
			if (field.id == 1 || field.id == 3) { const auto value = U32(field.payload); if (!value) return std::unexpected(value.error());
				if (field.id == 1) {spawn.id = *value; has_id = true;} else {spawn.definition = *value; has_definition = true;} }
			if (field.id == 2) { const auto value = Matrix(field.payload); if (!value) return std::unexpected(value.error()); spawn.transform = *value; has_transform = true; }
			if (field.id == 6) { const auto value = Flag(field.payload); if (!value) return std::unexpected(value.error()); spawn.enabled = *value; }
		}
		if (!has_id || !has_definition || !has_transform || !spawn_ids.insert(spawn.id).second)
			return std::unexpected("incomplete or duplicate spawner placement");
		result.spawners.push_back(spawn);
	}
	result.static_data.assign(static_bytes.begin(), static_bytes.end());
	result.dynamic_data.assign(dynamic_bytes.begin(), dynamic_bytes.end());
	auto dynamic = ReadDynamicLevel(dynamic_bytes);
	if (!dynamic) return std::unexpected(dynamic.error());
	const auto appended = AppendDynamicLevel(result.level, *dynamic);
	if (!appended) return std::unexpected(appended.error());
	result.dynamic = std::move(*dynamic);
	auto navigation = ReadNavigationPaths(static_bytes);
	if (!navigation) return std::unexpected(navigation.error());
	result.level.markers = std::move(navigation->markers);
	result.level.navigationPaths = std::move(navigation->paths);
	return result;
}

inline std::expected<PlayerStart, std::string> ResolvePlayerStart(const LevelScene &scene, const DefinitionCatalog &catalog)
{
	// SpawnManager::Get_Primary_Spawner/Get_Primary_Spawn_Location select the
	// first primary soldier-startup spawner in saved list order, even disabled.
	for (const auto &spawn : scene.spawners) {
		const auto *definition = catalog.Find(spawn.definition);
		if (!definition || !definition->spawner) return std::unexpected("spawner references a missing spawner definition");
		if (!definition->spawner->primary || !definition->spawner->soldier_startup) continue;
		if (definition->spawner->presets.empty()) return std::unexpected("primary spawner has no soldier preset");
		const auto *soldier = catalog.Find(definition->spawner->presets.front());
		if (!soldier || soldier->factory != 0x4010f) return std::unexpected("primary spawner preset is not a soldier");
		const auto *physics = catalog.Find(soldier->physics);
		if (!physics || physics->model.empty()) return std::unexpected("player physics definition has no model");
		return PlayerStart{spawn.id, soldier->id, physics->id, physics->model, spawn.transform};
	}
	return std::unexpected("no primary soldier-startup spawner");
}

inline std::expected<LevelScene, std::string> LoadLevelScene(const engine::filesystem::VirtualFileSystem &files, std::string_view map,
	const DefinitionCatalog &catalog)
{
	// Fresh-map entry only. Saved-game input will require its own dynamic bytes;
	// never substitute a retail LDD for a player's save.
	auto root = std::filesystem::path(map); root.replace_extension();
	const auto dynamic = files.Read(root.generic_string() + ".ldd");
	if (!dynamic) return std::unexpected("missing dynamic level " + root.generic_string());
	const auto info = persist::One(*dynamic, 0x3c51c460);
	if (!info) return std::unexpected(info.error());
	const auto fields = persist::Micros(info->payload);
	if (!fields) return std::unexpected(fields.error());
	std::string static_name;
	for (const auto &field : *fields) if (field.id == 1) { const auto value = persist::Text(field.payload); if (!value) return std::unexpected(value.error()); static_name = *value; }
	const auto statics = files.Read(static_name);
	if (!statics) return std::unexpected("missing static level " + static_name);
	auto result=ReadLevelScene(*statics, *dynamic);
	if(!result) return std::unexpected(result.error());
	const auto portals=AppendTraversalPortals(result->level,result->dynamic,catalog);
	if(!portals) return std::unexpected(portals.error());
	return result;
}
inline std::expected<LevelScene, std::string> LoadLevelScene(const engine::filesystem::VirtualFileSystem &files, std::string_view map)
{
	const auto bytes=files.Read("objects.ddb");if(!bytes) return std::unexpected("missing level definition catalog");
	const auto catalog=ReadDefinitions(*bytes);if(!catalog) return std::unexpected(catalog.error());
	return LoadLevelScene(files,map,*catalog);
}
}
