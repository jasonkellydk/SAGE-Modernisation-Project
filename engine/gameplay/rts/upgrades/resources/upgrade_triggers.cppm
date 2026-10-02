export module engine.gameplay.rts.upgrades.resources.upgrade_triggers;
import std;

export import engine.gameplay.rts.upgrades.definitions.upgrade_trigger;
import engine.ecs.system.system;

// Each kind of object's upgrade triggers, in its modules' order (definition
// data, indexed by DefinitionRef; filled by the game from its content).
export namespace engine::gameplay
{
struct UpgradeTriggers
{
	std::vector<std::vector<UpgradeTrigger>> byDefinition;

	const std::vector<UpgradeTrigger> *Of(std::uint32_t definition) const noexcept
	{
		return definition < byDefinition.size() && !byDefinition[definition].empty() ? &byDefinition[definition] : nullptr;
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::UpgradeTriggers>
{
	static constexpr std::string_view StableName = "engine.gameplay.upgrade_triggers";
};
}
