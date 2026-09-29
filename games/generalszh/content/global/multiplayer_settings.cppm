export module games.generalszh.content.global.multiplayer_settings;
import std;

export import engine.config.binding.schema;

// Multiplayer.ini (the original's MultiplayerSettings): the player colours in
// their order (a slot's colour is an index into them), the starting money
// choices (one the default) and the settings block.
export namespace generalszh::content
{
struct MultiplayerColor
{
	std::string name;        // ColorGold
	engine::config::Rgb day{};
	engine::config::Rgb night{};
	std::string tooltipName; // Color:Gold
};

struct MultiplayerSettings
{
	std::vector<MultiplayerColor> colors;
	std::vector<std::uint32_t> startingMoney; // in the file's order
	std::uint32_t defaultStartingMoney{10000};
	int startCountdownTimer{5};
	int maxBeaconsPerPlayer{3};
	bool useShroud{true};
	bool showRandomPlayerTemplate{true};
	bool showRandomStartPos{true};
	bool showRandomColor{true};
};

inline MultiplayerSettings BindMultiplayerSettings(const engine::config::Document &document, engine::config::BindContext &context)
{
	using namespace engine::config;
	MultiplayerSettings settings;
	bool sawDefault = false;
	for (const Node &root : document.Roots())
	{
		if (root.key == "MultiplayerColor")
		{
			MultiplayerColor color;
			color.name = std::string(root.values.empty() ? root.text : root.values.front());
			for (const Node &field : root.children)
				if (field.key == "RGBColor")
					color.day = ReadRgb(field, context).value_or(Rgb{});
				else if (field.key == "RGBNightColor")
					color.night = ReadRgb(field, context).value_or(Rgb{});
				else if (field.key == "TooltipName")
					color.tooltipName = ReadText(field);
			// MultiplayerSettings::findMultiplayerColorDefinitionByName: a name defined again is updated in place.
			bool found = false;
			for (MultiplayerColor &existing : settings.colors)
				if (existing.name == color.name)
					existing = color, found = true;
			if (!found)
				settings.colors.push_back(std::move(color));
		}
		else if (root.key == "MultiplayerStartingMoneyChoice")
		{
			std::uint32_t value = 0;
			bool isDefault = false;
			for (const Node &field : root.children)
				if (field.key == "Value")
					value = static_cast<std::uint32_t>(ReadInt(field, context).value_or(0));
				else if (field.key == "Default")
					isDefault = ReadBool(field, context).value_or(false);
			settings.startingMoney.push_back(value);
			if (isDefault && !sawDefault)
				settings.defaultStartingMoney = value, sawDefault = true;
		}
		else if (root.key == "MultiplayerSettings")
			for (const Node &field : root.children)
			{
				if (field.key == "StartCountdownTimer")
					settings.startCountdownTimer = static_cast<int>(ReadInt(field, context).value_or(5));
				else if (field.key == "MaxBeaconsPerPlayer")
					settings.maxBeaconsPerPlayer = static_cast<int>(ReadInt(field, context).value_or(3));
				else if (field.key == "UseShroud")
					settings.useShroud = ReadBool(field, context).value_or(true);
				else if (field.key == "ShowRandomPlayerTemplate")
					settings.showRandomPlayerTemplate = ReadBool(field, context).value_or(true);
				else if (field.key == "ShowRandomStartPos")
					settings.showRandomStartPos = ReadBool(field, context).value_or(true);
				else if (field.key == "ShowRandomColor")
					settings.showRandomColor = ReadBool(field, context).value_or(true);
			}
	}
	if (!sawDefault && !settings.startingMoney.empty())
		settings.defaultStartingMoney = settings.startingMoney.front();
	return settings;
}
}
