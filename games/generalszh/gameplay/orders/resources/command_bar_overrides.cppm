export module games.generalszh.gameplay.orders.resources.command_bar_overrides;
import std;

export import engine.core.serialization.byte_stream;
export import games.generalszh.content.control_bar.command_catalog;
import engine.ecs.system.system;

// The command set slots scripts changed (GameLogic::m_controlBarOverrides, COMMANDBAR_ADD_BUTTON_OBJECTTYPE_SLOT and
// COMMANDBAR_REMOVE_BUTTON_OBJECTTYPE): by set and slot, the button there now (empty: none). Every reading of a set's
// slot goes through them (CommandSet::getCommandButton), the player's control bar and the scripts' and AI's own.
// Simulation state: checkpointed and hashed.
export namespace generalszh::gameplay
{
struct CommandBarOverrides
{
	std::map<std::pair<std::string, std::uint32_t>, std::string, std::less<>> slots;

	void Set(const std::string &set, std::uint32_t slot, std::string button) { slots[{set, slot}] = std::move(button); }

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(slots.size()));
		for (const auto &[key, button] : slots)
		{
			writer.Text(key.first);
			writer.U32(key.second);
			writer.Text(button);
		}
	}
	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto count = reader.U32();
		if (!count)
			return false;
		std::map<std::pair<std::string, std::uint32_t>, std::string, std::less<>> loaded;
		for (std::uint32_t entry = 0; entry < *count; ++entry)
		{
			auto set = reader.Text();
			const auto slot = reader.U32();
			auto button = reader.Text();
			if (!set || !slot || !button)
				return false;
			loaded[{std::move(*set), *slot}] = std::move(*button);
		}
		slots = std::move(loaded);
		return true;
	}
};

// The set `name` as it reads now (ControlBar::findCommandSet, each slot through CommandSet::getCommandButton): none
// when there is no such set.
inline std::optional<content::CommandSetContent> EffectiveCommandSet(const content::CommandCatalog &catalog, const CommandBarOverrides *overrides,
	std::string_view name)
{
	const content::CommandSetContent *set = catalog.Set(name);
	if (set == nullptr)
		return std::nullopt;
	content::CommandSetContent effective = *set;
	if (overrides != nullptr)
		for (std::uint32_t slot = 0; slot < effective.buttons.size(); ++slot)
			if (const auto found = overrides->slots.find(std::pair{std::string(name), slot}); found != overrides->slots.end())
				effective.buttons[slot] = found->second;
	return effective;
}
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::CommandBarOverrides>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.command_bar_overrides";
};
}
