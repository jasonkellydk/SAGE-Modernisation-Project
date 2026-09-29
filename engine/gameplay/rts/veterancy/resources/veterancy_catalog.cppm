export module engine.gameplay.rts.veterancy.resources.veterancy_catalog;
import std;

export import engine.gameplay.rts.veterancy.definitions.veterancy;
export import Engine.Core.Math.Fixed;
import engine.ecs.system.system;

// Per definition (DefinitionRef::index) veterancy, and the game's veterancy
// settings: each level's max health multiplier, the object upgrade each level
// grants (none: 0xFFFFFFFF) and the weapon bonus condition bits it holds.
export namespace engine::gameplay
{
struct VeterancyCatalog
{
	static constexpr std::uint32_t NoUpgrade = 0xFFFFFFFFu;

	std::vector<VeterancyDefinition> byDefinition;
	std::array<Engine::Math::Fixed, VeterancyLevelCount> healthBonus{Engine::Math::Fixed::One(), Engine::Math::Fixed::One(),
		Engine::Math::Fixed::One(), Engine::Math::Fixed::One()};
	std::array<std::uint32_t, VeterancyLevelCount> levelUpgrade{NoUpgrade, NoUpgrade, NoUpgrade, NoUpgrade};
	// The weapon bonus condition bits each level holds (and the others' it drops), set in level order.
	std::array<std::uint32_t, VeterancyLevelCount> levelBonus{};

	const VeterancyDefinition &Of(std::uint32_t definition) const noexcept
	{
		static const VeterancyDefinition none{};
		return definition < byDefinition.size() ? byDefinition[definition] : none;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::VeterancyCatalog>
{
	static constexpr std::string_view StableName = "engine.gameplay.veterancy_catalog";
};
}
