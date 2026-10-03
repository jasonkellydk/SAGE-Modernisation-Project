export module games.renegade.content.levels.definition_catalog;
import std;
export import games.renegade.content.levels.persist_records;
export import games.renegade.content.levels.transition_definitions;

export namespace renegade::content
{
struct SpawnerDefinition
{
	std::vector<std::uint32_t> presets;
	bool primary{}, soldier_startup{};
};
struct ScriptZoneDefinition
{
	bool check_stars_only{}, environment{};
	std::uint32_t type{};
};
struct LegacyDefinition
{
	std::uint32_t id{}, factory{}, physics{};
	std::string name, model;
	std::optional<SpawnerDefinition> spawner;
	// Retain every definition variant, including schemas not yet interpreted.
	std::vector<std::byte> data;
	std::optional<ScriptZoneDefinition> zone;
	bool editor_only{},hidden{};
	std::vector<TransitionDefinition> transitions;
};
struct DefinitionCatalog
{
	std::map<std::uint32_t, LegacyDefinition> by_id;
	const LegacyDefinition *Find(std::uint32_t id) const noexcept {
		const auto found = by_id.find(id); return found == by_id.end() ? nullptr : &found->second;
	}
};

inline std::expected<DefinitionCatalog, std::string> ReadDefinitions(persist::Bytes bytes)
{
	using namespace persist;
	const auto manager = One(bytes, 0x101);
	if (!manager) return std::unexpected(manager.error());
	const auto objects = One(manager->payload, 0x101);
	if (!objects) return std::unexpected(objects.error());
	const auto factories = Children(objects->payload);
	if (!factories) return std::unexpected(factories.error());
	DefinitionCatalog result;
	for (const auto &factory : *factories) {
		const auto data = ObjectData(factory.payload);
		if (!data) return std::unexpected(data.error());
		LegacyDefinition definition; definition.factory = factory.id;
		definition.data.assign(data->payload.begin(), data->payload.end());
		const auto identities = Descendants(data->payload, 0x100);
		if (!identities) return std::unexpected(identities.error());
		unsigned identity_count{};
		for (const auto &identity : *identities) {
			const auto fields = Micros(identity.payload);
			if (!fields) return std::unexpected(fields.error());
			std::optional<std::uint32_t> id; std::optional<std::string> name;
			for (const auto &field : *fields) {
				if (field.id == 1) { const auto value = U32(field.payload); if (value) id = *value; }
				if (field.id == 3) { const auto value = Text(field.payload); if (value) name = *value; }
			}
			if (id && name) { definition.id = *id; definition.name = *name; ++identity_count; }
		}
		if (identity_count != 1) return std::unexpected("definition has ambiguous or missing identity");
		const auto decode_fields = [&](std::uint32_t chunk_id, auto &&decode) -> std::expected<void, std::string> {
			const auto records = chunk_id == 909991657 ? ScopedVariables(data->payload, 909991661, chunk_id) : Descendants(data->payload, chunk_id);
			if (!records) return std::unexpected(records.error());
			if (records->size() > 1) return std::unexpected("duplicate definition schema variables");
			if (records->empty()) return {};
			const auto fields = Micros(records->front().payload);
			if (!fields) return std::unexpected(fields.error());
			for (const auto &field : *fields) { const auto status = decode(field); if (!status) return status; }
			return {};
		};
		// wwphys/phys.cpp PhysDefClass: model file; physicalgameobj.cpp:
		// physical game-object -> physics definition link (microchunk 18).
		const auto model = decode_fields(0x055ffe08, [&](const Micro &field) -> std::expected<void, std::string> {
			if (field.id == 1) { auto value = Text(field.payload); if (!value) return std::unexpected(value.error()); definition.model = std::move(*value); }
			return {};
		});
		if (!model) return std::unexpected(model.error());
		const auto physics = decode_fields(909991657, [&](const Micro &field) -> std::expected<void, std::string> {
			if (field.id == 18) { auto value = U32(field.payload); if (!value) return std::unexpected(value.error()); definition.physics = *value; }
			return {};
		});
		if (!physics) return std::unexpected(physics.error());
		// SimpleGameObj::On_Post_Load removes editor models and hides hidden
		// presets. Their script/physics identity still exists in the scene.
		// VehicleGameObjDef reuses these IDs with a different wire schema.
		// Only the SimpleGameObjDef inheritance family owns these booleans.
		const bool simple_family=factory.id==0x40103 || factory.id==0x40107 || factory.id==0x4010b || factory.id==0x40136;
		const auto simple=simple_family ? ScopedVariables(data->payload,930991656,930991657) : std::expected<std::vector<Chunk>,std::string>{std::vector<Chunk>{}};
		if(!simple || simple->size()>1) return std::unexpected("invalid simple-object presentation definition");
		if(!simple->empty()) {
			const auto fields=Micros(simple->front().payload);if(!fields) return std::unexpected(fields.error());std::set<unsigned> seen;
			for(const auto& field:*fields) if(field.id==1 || field.id==2) {
				if(!seen.insert(field.id).second) return std::unexpected("duplicate simple-object visibility field");
				const auto value=Flag(field.payload);if(!value) return std::unexpected(value.error());(field.id==1 ? definition.editor_only : definition.hidden)=*value;
			}
		}
		const auto zone_records = Descendants(data->payload, 1111991133);
		if (!zone_records || zone_records->size() > 1) return std::unexpected("invalid script-zone definition");
		if (!zone_records->empty()) {
			definition.zone.emplace();
			const auto zone = decode_fields(1111991133, [&](const Micro &field) -> std::expected<void, std::string> {
				if (field.id == 3 || field.id == 5) {const auto value = Flag(field.payload); if (!value) return std::unexpected(value.error()); (field.id == 3 ? definition.zone->check_stars_only : definition.zone->environment) = *value;}
				if (field.id == 4) {const auto value = U32(field.payload); if (!value) return std::unexpected(value.error()); definition.zone->type = *value;}
				return {};
			});
			if (!zone) return std::unexpected(zone.error());
		}
		if (factory.id == 0x40121) {
			definition.spawner.emplace();
			const auto spawn = decode_fields(1013991543, [&](const Micro &field) -> std::expected<void, std::string> {
				if (field.id == 1) { const auto value = U32(field.payload); if (!value) return std::unexpected(value.error()); definition.spawner->presets.push_back(*value); }
				if (field.id == 9 || field.id == 15) { const auto value = Flag(field.payload); if (!value) return std::unexpected(value.error()); (field.id == 9 ? definition.spawner->primary : definition.spawner->soldier_startup) = *value; }
				return {};
			});
			if (!spawn) return std::unexpected(spawn.error());
		}
		if (factory.id == 0x40125) {
			auto transitions=ReadTransitionDefinitions(data->payload);
			if(!transitions) return std::unexpected(transitions.error());
			definition.transitions=std::move(*transitions);
		}
		if (!result.by_id.emplace(definition.id, std::move(definition)).second) return std::unexpected("duplicate definition ID");
	}
	return result;
}
}
