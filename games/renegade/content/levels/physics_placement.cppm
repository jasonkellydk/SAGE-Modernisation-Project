export module games.renegade.content.levels.physics_placement;
import std;
export import games.renegade.content.levels.persist_records;

export namespace renegade::content
{
struct PhysicsPlacement
{
	std::uint32_t factory{}, token{}, definition{}, instance{}, flags{};
	std::string model;
	Engine::Math::FixedAffineTransform3 transform;
	std::vector<std::byte> user_lighting;
	// RenderObj versus Dazzle persistence is a game content schema distinction.
	std::uint32_t render_factory{0x10000};
	// PhysClass registers its Cullable/WidgetUser/Editable base pointers as
	// separate aliases. Saved game-object links can name any of these, rather
	// than the most-derived SimplePersistFactory token.
	std::array<std::uint32_t, 3> remap_tokens{};
};
inline std::expected<PhysicsPlacement, std::string> ReadPhysicsPlacement(const persist::Chunk &factory)
{
	using namespace persist;
	const auto object = ObjectData(factory.payload);
	const auto pointer = One(factory.payload, 0x00100100);
	if (!object || !pointer) return std::unexpected("invalid physical object factory");
	const auto token = U32(pointer->payload); if (!token) return std::unexpected(token.error());
	PhysicsPlacement result; result.factory = factory.id; result.token = *token;
	const auto physics = Descendants(object->payload, 0x00660055);
	const auto model_wrappers = Descendants(object->payload, 0x00660056);
	if (!physics || !model_wrappers || physics->size() != 1 || model_wrappers->size() != 1)
		return std::unexpected("physical object requires one PhysClass and one model wrapper");
	const auto model_factories = Children(model_wrappers->front().payload);
	if (!model_factories || model_factories->size() != 1) return std::unexpected("invalid physical render factory");
	result.render_factory = model_factories->front().id;
	const auto model_variables = result.render_factory == 0x10000 ? 0x00555040u : result.render_factory == 0x10002 ? 1212000336u : 0u;
	if (!model_variables) return std::unexpected("unsupported physical render factory " + std::to_string(result.render_factory));
	const auto model = One(model_factories->front().payload, model_variables);
	if (!model) return std::unexpected(model.error());
	const auto fields = Micros(physics->front().payload);
	if (!fields) return std::unexpected(fields.error());
	bool has_flags{}, has_instance{};
	for (const auto &field : *fields) if (field.id <= 3 || field.id == 6 || field.id == 7) {
		const auto value = U32(field.payload); if (!value) return std::unexpected(value.error());
		if (field.id < 3) result.remap_tokens[field.id] = *value;
		if (field.id == 3) { result.flags = *value; has_flags = true; }
		if (field.id == 6) result.definition = *value;
		if (field.id == 7) { result.instance = *value; has_instance = true; }
	}
	if (!has_flags || !has_instance) return std::unexpected("missing physical instance identity or flags");
	const auto model_fields = Micros(model->payload);
	if (!model_fields) return std::unexpected(model_fields.error());
	bool has_name{}, has_transform{};
	for (const auto &field : *model_fields) {
		if (field.id == (result.render_factory == 0x10002 ? 3 : 1)) { const auto value = Text(field.payload); if (!value) return std::unexpected(value.error()); result.model = *value; has_name = true; }
		if (field.id == 2) { const auto value = Matrix(field.payload); if (!value) return std::unexpected(value.error()); result.transform = *value; has_transform = true; }
	}
	if (!has_name || !has_transform) return std::unexpected("missing persisted render name or transform");
	const auto lighting = Descendants(object->payload, 0x00555041);
	if (!lighting || lighting->size() > 1) return std::unexpected("invalid persisted user lighting");
	if (!lighting->empty()) result.user_lighting.assign(lighting->front().payload.begin(), lighting->front().payload.end());
	return result;
}

}
