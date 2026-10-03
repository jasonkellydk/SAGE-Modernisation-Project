export module games.renegade.content.campaign.campaign_content;
import std;
export import games.renegade.content.cameras.camera_profiles;
export import games.renegade.content.armor.defense_preset;
export import games.renegade.content.weapons.weapon_catalog;
export import games.renegade.content.presentation.hud_settings;
import engine.filesystem.core.virtual_file_system;

export namespace renegade::content
{
// Typed, immutable content assembled once by the host composition root.
// Simulation and presentation consume these values without INI documents,
// filenames, key lookups or ad hoc configuration parsing.
struct CampaignContent
{
	DefinitionCatalog definitions;
	ArmorCatalog armor;
	CameraProfiles cameras;
	WeaponCatalog weapons;
	HudSettings hud;
};

inline std::expected<CampaignContent, std::string> LoadCampaignContent(const engine::filesystem::VirtualFileSystem &files)
{
	const auto objects = files.Read("objects.ddb");
	if (!objects) return std::unexpected("Objects.DDB missing");
	auto definitions = ReadDefinitions(*objects);
	if (!definitions) return std::unexpected(definitions.error());
	auto armor = LoadArmorCatalog(files);
	if (!armor) return std::unexpected(armor.error());
	const auto text = files.ReadText("cameras.ini");
	if (!text) return std::unexpected("cameras.ini missing");
	auto cameras = ReadCameraProfiles(*text);
	if (!cameras) return std::unexpected(cameras.error());
	if (!cameras->Find("default")) return std::unexpected("default camera profile missing");
	auto weapons=ReadWeaponCatalog(*definitions);if(!weapons) return std::unexpected(weapons.error());
	auto hud=ReadHudSettings(*definitions);if(!hud) return std::unexpected(hud.error());
	return CampaignContent{std::move(*definitions), std::move(*armor), std::move(*cameras),std::move(*weapons),std::move(*hud)};
}
}
