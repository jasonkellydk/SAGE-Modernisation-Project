export module engine.gameplay.common.identity.resources.name_registry;
import std;

export import engine.ecs.core.entity;
import engine.ecs.system.system;
export import engine.ecs.core.entity_codec;

// Script names of entities ("Commanche1" -> entity). A name refers to one
// entity at a time; naming another entity takes the name over. Written by
// the session between ticks, read by systems.
export namespace engine::gameplay
{
class NameRegistry
{
public:
	void Assign(std::string name, ecs::Entity entity)
	{
		if (name.empty())
			return;
		m_known.insert(name);
		m_byName.insert_or_assign(std::move(name), entity);
	}

	// Whether anything ever had the name (ScriptEngine::didUnitExist: a name stays known once its unit is gone).
	bool Known(std::string_view name) const { return m_known.find(name) != m_known.end(); }

	// A name for the entity itself, whatever it is called ("#entity:<index>:<generation>"): what Find gives back for
	// "<This Object>" in a script run on that entity.
	static std::string Reference(ecs::Entity entity)
	{
		return "#entity:" + std::to_string(entity.index) + ":" + std::to_string(entity.generation);
	}

	ecs::Entity Find(std::string_view name) const
	{
		if (name.starts_with("#entity:"))
		{
			const std::string_view rest = name.substr(8);
			const std::size_t colon = rest.find(':');
			std::uint64_t index = 0, generation = 0;
			if (colon == std::string_view::npos || std::from_chars(rest.data(), rest.data() + colon, index).ec != std::errc{} ||
				std::from_chars(rest.data() + colon + 1, rest.data() + rest.size(), generation).ec != std::errc{})
				return {};
			ecs::Entity entity;
			entity.index = static_cast<decltype(entity.index)>(index);
			entity.generation = static_cast<decltype(entity.generation)>(generation);
			return entity;
		}
		const auto found = m_byName.find(name);
		return found == m_byName.end() ? ecs::Entity{} : found->second;
	}

	// Its names go (kept among this tick's released names: what takes its place may take them, as
	// ScriptEngine::transferObjectName hands a dead building's name to its rebuild hole).
	void Forget(ecs::Entity entity)
	{
		for (auto it = m_byName.begin(); it != m_byName.end();)
			if (it->second == entity)
			{
				m_released.emplace_back(it->first, entity);
				it = m_byName.erase(it);
			}
			else
				++it;
	}
	// The name an entity gone this tick had, if any.
	std::optional<std::string> Released(ecs::Entity entity) const
	{
		for (const auto &[name, gone] : m_released)
			if (gone == entity)
				return name;
		return std::nullopt;
	}
	// Each tick starts with none released (a per-tick record: not saved).
	void ClearReleased() noexcept { m_released.clear(); }
	// The name an entity has, if any.
	std::optional<std::string> NameOf(ecs::Entity entity) const
	{
		for (const auto &[name, named] : m_byName)
			if (named == entity)
				return name;
		return std::nullopt;
	}

	std::size_t Size() const noexcept { return m_byName.size(); }

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(m_byName.size()));
		for (const auto &[name, entity] : m_byName)
		{
			writer.Text(name);
			ecs::WriteEntity(writer, entity);
		}
		writer.U32(static_cast<std::uint32_t>(m_known.size()));
		for (const std::string &name : m_known)
			writer.Text(name);
	}

	bool Load(engine::core::serialization::ByteReader &reader)
	{
		std::map<std::string, ecs::Entity, std::less<>> names;
		const auto count = reader.U32();
		for (std::uint32_t index = 0; count && index < *count && !reader.Failed(); ++index)
		{
			std::string name = reader.Text().value_or("");
			names.emplace(std::move(name), ecs::ReadEntity(reader).value_or(ecs::Entity{}));
		}
		std::set<std::string, std::less<>> known;
		const auto knownCount = reader.U32();
		for (std::uint32_t index = 0; knownCount && index < *knownCount && !reader.Failed(); ++index)
			known.insert(reader.Text().value_or(""));
		if (reader.Failed() || !count || !knownCount)
			return false;
		m_byName = std::move(names);
		m_known = std::move(known);
		return true;
	}

private:
	std::vector<std::pair<std::string, ecs::Entity>> m_released;
	std::map<std::string, ecs::Entity, std::less<>> m_byName;
	std::set<std::string, std::less<>> m_known;
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::NameRegistry>
{
	static constexpr std::string_view StableName = "engine.gameplay.name_registry";
};
}
