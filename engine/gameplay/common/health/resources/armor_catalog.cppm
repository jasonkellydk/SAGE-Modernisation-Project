export module engine.gameplay.common.health.resources.armor_catalog;
import std;

export import engine.gameplay.common.health.definitions.armor;
import engine.ecs.system.system;

// Every armor in play, indexed by Health::armor. Index 0 is plain (no
// reduction) so entities without armor data still take full damage. With
// them, the damage types no body loses health to (ActiveBody::attemptDamage's
// alreadyHandled cases): the game carries them out from the tick's Hits; and the subdual damage types (IsSubdualDamage),
// which a body that can be subdued takes as subdual damage instead of health, and any other not at all.
export namespace engine::gameplay
{
class ArmorCatalog
{
public:
	ArmorCatalog() : m_armors(1) {}

	std::uint32_t Add(ArmorDefinition armor)
	{
		for (std::uint32_t type = 0; type < MaxDamageTypes; ++type)
			if ((armor.bypass >> type & 1u) == 0 && armor.coefficient[type] <= Engine::Math::Fixed{})
				m_stopped |= std::uint64_t{1} << type;
		m_armors.push_back(armor);
		return static_cast<std::uint32_t>(m_armors.size() - 1);
	}

	const ArmorDefinition &At(std::uint32_t index) const noexcept { return index < m_armors.size() ? m_armors[index] : m_armors.front(); }
	std::size_t Size() const noexcept { return m_armors.size(); }

	void SetHandled(std::uint64_t damageTypes) noexcept { m_handled = damageTypes; }
	bool Handled(std::uint32_t damageType) const noexcept { return damageType < 64 && (m_handled >> damageType & 1u) != 0; }
	void SetSubdual(std::uint64_t damageTypes) noexcept { m_subdual = damageTypes; }
	bool Subdual(std::uint32_t damageType) const noexcept { return damageType < 64 && (m_subdual >> damageType & 1u) != 0; }
	// The damage types a body's damage scalar leaves alone (UNRESISTABLE: "just like the armor code can't").
	void SetUnscaled(std::uint64_t damageTypes) noexcept { m_unscaled = damageTypes; }
	bool Unscaled(std::uint32_t damageType) const noexcept { return damageType < 64 && (m_unscaled >> damageType & 1u) != 0; }
	// Whether some armor lets none of this damage type through (else any armor lets some through).
	bool SomeStop(std::uint32_t damageType) const noexcept { return damageType >= 64 || (m_stopped >> damageType & 1u) != 0; }

private:
	std::vector<ArmorDefinition> m_armors;
	std::uint64_t m_handled{0};
	std::uint64_t m_subdual{0};
	std::uint64_t m_unscaled{0};
	std::uint64_t m_stopped{0};
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::ArmorCatalog>
{
	static constexpr std::string_view StableName = "engine.gameplay.armor_catalog";
};
}
