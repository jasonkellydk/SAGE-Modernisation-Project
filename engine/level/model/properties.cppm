export module engine.level.model.properties;
import std;

export import Engine.Core.Math.Fixed;

export namespace engine::level
{
namespace math = Engine::Math;

using PropertyValue = std::variant<bool, std::int64_t, math::Fixed, std::string>;

// Ordered key/value data attached to a level, a placement or a marker
// (e.g. "objectInitialHealth" = 100, "originalOwner" = "PlyrCivilian").
// Order is preserved as authored so re-saving and hashing are stable.
class Properties
{
public:
	void Set(std::string key, PropertyValue value)
	{
		for (auto &[existing, current] : m_entries)
			if (existing == key)
			{
				current = std::move(value);
				return;
			}
		m_entries.emplace_back(std::move(key), std::move(value));
	}

	const PropertyValue *Find(std::string_view key) const noexcept
	{
		for (const auto &[existing, value] : m_entries)
			if (existing == key)
				return &value;
		return nullptr;
	}

	template<typename T>
	std::optional<T> Get(std::string_view key) const
	{
		const PropertyValue *value = Find(key);
		if (value == nullptr || !std::holds_alternative<T>(*value))
			return std::nullopt;
		return std::get<T>(*value);
	}

	bool Contains(std::string_view key) const noexcept { return Find(key) != nullptr; }
	std::size_t Size() const noexcept { return m_entries.size(); }
	bool Empty() const noexcept { return m_entries.empty(); }
	auto begin() const noexcept { return m_entries.begin(); }
	auto end() const noexcept { return m_entries.end(); }

private:
	std::vector<std::pair<std::string, PropertyValue>> m_entries;
};
}
