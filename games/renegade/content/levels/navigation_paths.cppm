export module games.renegade.content.levels.navigation_paths;
import std;
export import games.renegade.content.levels.persist_records;
export import engine.level.model.level;

export namespace renegade::content
{
struct NavigationPaths
{
	std::vector<engine::level::Marker> markers;
	std::vector<engine::level::NavigationPath> paths;
};

inline std::expected<NavigationPaths, std::string> ReadNavigationPaths(persist::Bytes static_bytes)
{
	using namespace persist;
	// PathfindClass::Save and the Waypath/Waypoint SimplePersist factories.
	// Serialized addresses are reference tokens, never native pointers. Resolve
	// the ordered membership list after reading every point, as Post_Load does.
	const auto subsystem = One(static_bytes, 0x20000);
	if (!subsystem) return std::unexpected(subsystem.error());
	const auto pathfind = One(subsystem->payload, 0x04433221);
	if (!pathfind) return std::unexpected(pathfind.error());
	const auto database = One(pathfind->payload, 0x01060635);
	if (!database) return std::unexpected(database.error());
	const auto records = Children(database->payload);
	if (!records) return std::unexpected(records.error());
	NavigationPaths result;
	struct Membership { std::vector<std::uint32_t> tokens; };
	std::vector<Membership> memberships;
	std::map<std::uint32_t, std::uint32_t> marker_tokens;
	std::set<std::uint32_t> identities, tokens;
	for (const auto &record : *records) {
		if (record.id != 0x20110 && record.id != 0x20111) continue;
		const bool path = record.id == 0x20110;
		const auto object = One(record.payload, 0x100101);
		if (!object) return std::unexpected(object.error());
		const auto variables = One(object->payload, path ? 0x04290219 : 0x04290112);
		if (!variables) return std::unexpected(variables.error());
		const auto fields = Micros(variables->payload);
		if (!fields) return std::unexpected(fields.error());
		std::set<std::uint8_t> seen;
		std::uint32_t token{}, flags{}, id{}, portal{0xffffffffu};
		Engine::Math::FixedVector3 position;
		Membership membership;
		for (const auto &field : *fields) {
			if (field.id < 1 || field.id > (path ? 4 : 5)) continue;
			if (!(path && field.id == 4) && !seen.insert(field.id).second)
				return std::unexpected("duplicate navigation variable");
			if (!path && field.id == 3) {
				const auto value = Vector(field.payload); if (!value) return std::unexpected(value.error()); position = *value;
			} else {
				const auto value = U32(field.payload); if (!value) return std::unexpected(value.error());
				if (field.id == 1) token = *value;
				else if (field.id == 2) flags = *value;
				else if (path && field.id == 3 || !path && field.id == 4) id = *value;
				else if (path) membership.tokens.push_back(*value);
				else portal = *value;
			}
		}
		if (!seen.contains(1) || !seen.contains(2) || !seen.contains(3) || !path && (!seen.contains(4) || !seen.contains(5)))
			return std::unexpected("incomplete navigation record");
		if (!token || !tokens.insert(token).second || !identities.insert(id).second)
			return std::unexpected("duplicate navigation identity or reference token");
		if (path) {
			engine::level::NavigationPath output; output.id = id; output.looping = (flags & 2u) != 0;
			output.properties.Set("renegade.flags", std::int64_t(flags));
			result.paths.push_back(std::move(output)); memberships.push_back(std::move(membership));
		} else {
			engine::level::Marker output; output.id = id; output.position = position;
			output.properties.Set("renegade.flags", std::int64_t(flags));
			output.properties.Set("renegade.action_portal", std::int64_t(std::bit_cast<std::int32_t>(portal)));
			marker_tokens.emplace(token, id); result.markers.push_back(std::move(output));
		}
	}
	for (std::size_t index = 0; index < result.paths.size(); ++index) {
		for (const auto token : memberships[index].tokens) {
			const auto point = marker_tokens.find(token);
			if (point == marker_tokens.end()) return std::unexpected("navigation path references a missing waypoint token");
			result.paths[index].markers.push_back(point->second);
		}
	}
	return result;
}
}
