export module games.renegade.content.armor.armor_catalog;
import std;
export import engine.config.adapters.ini.section_reader;
export import games.renegade.gameplay.defense.resources.damage_rules;

export namespace renegade::content
{
struct ArmorCatalog
{
	DamageRules rules;
	std::vector<std::string> armors;
	std::vector<std::string> warheads;
	std::vector<std::int32_t> armorSaveIds;
	std::vector<std::int32_t> warheadSaveIds;
	std::vector<bool> soft;
	std::optional<std::uint32_t> Armor(std::string_view name) const
	{
		const auto found = std::ranges::find_if(armors, [&](const auto &item) { return engine::config::ini::FoldAscii(item) == engine::config::ini::FoldAscii(name); });
		return found == armors.end() ? std::nullopt : std::optional(static_cast<std::uint32_t>(found - armors.begin()));
	}
	std::optional<std::uint32_t> Warhead(std::string_view name) const
	{
		const auto found = std::ranges::find_if(warheads, [&](const auto &item) { return engine::config::ini::FoldAscii(item) == engine::config::ini::FoldAscii(name); });
		return found == warheads.end() ? std::nullopt : std::optional(static_cast<std::uint32_t>(found - warheads.begin()));
	}
};
// Combat/damage.cpp ArmorWarheadManager::Init: declaration-order indices,
// persistent save IDs, soft armor membership, default scale=1/absorption=0.
// Special damage metadata remains a separately tracked migration slice.
std::expected<ArmorCatalog, std::string> ReadArmor(std::string text)
{
	using namespace engine::config;
	auto document = ini::ReadSections("armor.ini", std::move(text));
	if (!document) return std::unexpected(document.error());
	ArmorCatalog catalog;
	const auto names = [&](std::string_view section, std::vector<std::string> &out) -> bool {
		const auto *node = ini::FindSection(*document, section);
		if (!node || node->children.empty()) return false;
		for (const auto &child : node->children)
		{
			if (child.text.empty()) return false;
			for (const auto &name : out) if (ini::FoldAscii(name) == ini::FoldAscii(child.text)) return false;
			out.emplace_back(child.text);
		}
		return true;
	};
	if (!names("Armor_Types", catalog.armors) || !names("Warhead_Types", catalog.warheads))
		return std::unexpected("armor.ini requires unique armor and warhead names");
	const auto value = [&](std::string_view section, std::string_view key) -> std::optional<std::string_view> {
		const auto *node = ini::FindSection(*document, section);
		if (!node) return {};
		const auto *child = node->Find(ini::FoldAscii(key));
		return child ? std::optional(child->text) : std::nullopt;
	};
	const auto ids = [&](std::string_view section, const std::vector<std::string> &names, std::vector<std::int32_t> &out) -> bool {
		for (const auto &name : names)
		{
			const auto text = value(section, name);
			if (!text) { out.push_back(-100); continue; } // original missing-ID sentinel
			std::int32_t id{};
			const auto parsed = std::from_chars(text->data(), text->data() + text->size(), id);
			if (parsed.ec != std::errc{} || parsed.ptr != text->data() + text->size()) return false;
			out.push_back(id);
		}
		return true;
	};
	if (!ids("Armor_Save_IDs", catalog.armors, catalog.armorSaveIds) || !ids("Warhead_Save_IDs", catalog.warheads, catalog.warheadSaveIds))
		return std::unexpected("invalid armor.ini save ID");
	catalog.soft.resize(catalog.armors.size());
	if (const auto *soft = ini::FindSection(*document, "Soft_Armor"))
		for (const auto &child : soft->children)
		{
			const auto armor = catalog.Armor(child.text);
			if (!armor) return std::unexpected("unknown soft armor: " + std::string(child.text));
			catalog.soft[*armor] = true;
		}
	for (std::uint32_t armor = 0; armor < catalog.armors.size(); ++armor)
		for (std::uint32_t warhead = 0; warhead < catalog.warheads.size(); ++warhead)
		{
			ArmorResponse response;
			const auto multiplier = value("Scale_" + catalog.armors[armor], catalog.warheads[warhead]);
			const auto absorption = value("Shield_" + catalog.armors[armor], catalog.warheads[warhead]);
			if (multiplier)
			{
				const auto parsed = Fixed::ParseDecimal(*multiplier);
				if (!parsed) return std::unexpected("invalid armor.ini multiplier");
				response.multiplier = *parsed;
			}
			if (absorption)
			{
				const auto parsed = Fixed::ParseDecimal(*absorption);
				if (!parsed) return std::unexpected("invalid armor.ini absorption");
				response.absorption = *parsed;
			}
			catalog.rules.responses[{armor, warhead}] = response;
		}
	return catalog;
}
}
