export module games.generalszh.content.loading.map_overrides;
import std;

export import games.generalszh.content.loading.game_content;

// GameLogic::startNewGame's load of the map's map.ini (INI_LOAD_CREATE_OVERRIDES), for one match: the game's content
// with the map's blocks over it. How a block meets a definition the game already has is each store's own:
//   ThingFactory (Object, ObjectReskin): ApplyObjectOverrides.
//   LocomotorStore, WeaponStore, ScienceStore, SpecialPowerStore, ControlBar (CommandSet), CrateSystem (CrateData):
//     newOverride, a copy of it with the block's fields parsed over it; UpgradeCenter parses the block over the upgrade
//     itself (the same, but for good: the original kept a map's upgrade changes after the match, a leak not kept here).
//   ObjectCreationListStore: ocl.clear(), the block alone (for good in the original; here for the match).
//   AI (AIData): newOverride, a copy of the game's AI data, the block over it (its SideInfo onto the side's,
//     SkirmishBuildList replacing the side's).
// A name the game lacks is a new definition after the game's. The overrides go with the match (GameLogic::reset).
export namespace generalszh::content
{
enum class OverrideRule : std::uint8_t
{
	Merge,   // the game's lines but those the block sets, then the block's
	Replace, // the block alone, where the game's stood
	Append,  // the block after the game's (one unnamed block: AIData)
};

struct OverrideKind
{
	std::string_view block;
	OverrideRule rule;
};

inline constexpr std::array<OverrideKind, 9> MapOverrideKinds{{
	{"Locomotor", OverrideRule::Merge},
	{"Weapon", OverrideRule::Merge},
	{"Science", OverrideRule::Merge},
	{"SpecialPower", OverrideRule::Merge},
	{"CommandSet", OverrideRule::Merge},
	{"CrateData", OverrideRule::Merge},
	{"Upgrade", OverrideRule::Merge},
	{"ObjectCreationList", OverrideRule::Replace},
	{"AIData", OverrideRule::Append},
}};

// Fields a block adds to rather than sets (one veterancy level at a time; WeaponBonus, ScatterTarget and CrateObject
// lists): parsed over a copy, the copy's own stay, so the game's lines stay ahead of the block's.
inline constexpr std::array<std::string_view, 8> AccumulatingKeys{"VeterancyFireFX", "VeterancyProjectileDetonationFX", "VeterancyFireOCL",
	"VeterancyProjectileDetonationOCL", "VeterancyProjectileExhaust", "WeaponBonus", "ScatterTarget", "CrateObject"};

inline bool HasBlocks(const engine::config::Document &document, std::string_view block)
{
	return std::ranges::any_of(document.Roots(), [&](const engine::config::Node &root) { return root.key == block; });
}

// A copy of `base`'s blocks with `overlay`'s blocks of type `block` laid over them by `rule` (the nodes still point into
// both documents' text, which the loader keeps).
inline std::shared_ptr<const engine::config::Document> OverlayDocument(const engine::config::Document &base, const engine::config::Document &overlay,
	std::string_view block, OverrideRule rule)
{
	auto document = std::make_shared<engine::config::Document>();
	std::vector<engine::config::Node> &roots = document->Roots();
	roots = base.Roots();
	for (const engine::config::Node &over : overlay.Roots())
	{
		if (over.key != block)
			continue;
		const auto found = rule == OverrideRule::Append ? roots.end()
			: std::find_if(roots.begin(), roots.end(), [&](const engine::config::Node &root) { return root.key == block && root.Value() == over.Value(); });
		if (found == roots.end())
		{
			roots.push_back(over);
			continue;
		}
		if (rule == OverrideRule::Replace)
		{
			*found = over;
			continue;
		}
		std::vector<engine::config::Node> children;
		for (const engine::config::Node &child : found->children)
		{
			const bool accumulates = std::ranges::find(AccumulatingKeys, child.key) != AccumulatingKeys.end();
			if (accumulates || over.Find(child.key) == nullptr)
				children.push_back(child);
		}
		children.insert(children.end(), over.children.begin(), over.children.end());
		found->children = std::move(children);
	}
	return document;
}

// Whether a map's map.ini overrides anything of the game's content.
inline bool HasMapOverrides(const engine::config::Document &mapIni)
{
	for (const engine::config::Node &root : mapIni.Roots())
		if (root.key == "Object" || root.key == "ObjectReskin" ||
			std::ranges::any_of(MapOverrideKinds, [&](const OverrideKind &kind) { return kind.block == root.key; }))
			return true;
	return false;
}

// The game's content with the map's map.ini over it, and what the changed definitions give derived again; the rest is
// the game's own.
GameContent WithMapOverrides(const GameContent &base, const engine::config::Document &mapIni, ContentLoader &loader, const engine::time::FixedStep &step)
{
	GameContent content = base;
	engine::config::BindContext context{loader.DiagnosticsFor(mapIni), step};
	const auto overlay = [&](const engine::config::Document *source, std::string_view block) -> const engine::config::Document * {
		if (source == nullptr || !HasBlocks(mapIni, block))
			return nullptr;
		const auto kind = std::ranges::find(MapOverrideKinds, block, &OverrideKind::block);
		content.overlays.push_back(OverlayDocument(*source, mapIni, block, kind->rule));
		return content.overlays.back().get();
	};
	std::vector<std::string> changedObjects = ApplyObjectOverrides(content.objects, mapIni, context);

	std::set<std::string, std::less<>> changedLocomotors;
	if (const engine::config::Document *locomotors = overlay(base.sources.locomotors, "Locomotor"))
	{
		content.locomotors = BuildLocomotorCatalog(*locomotors, context);
		content.wheelTurnAngles = ReadWheelTurnAngles(*locomotors);
		content.chassisLooks = ReadChassisLooks(*locomotors);
		content.hoverLocomotors = ReadHoverLocomotors(*locomotors, step);
		for (const engine::config::Node &root : mapIni.Roots())
			if (root.key == "Locomotor")
				changedLocomotors.emplace(root.Value());
	}
	if (const engine::config::Document *weapons = overlay(base.sources.weapons, "Weapon"))
		content.weapons = BuildWeaponCatalog(*weapons, context);
	const engine::config::Document *creationLists = overlay(base.sources.creationLists, "ObjectCreationList");
	const engine::config::Document *specialPowers = overlay(base.sources.specialPowers, "SpecialPower");
	if (creationLists != nullptr && base.sources.commandButtons != nullptr)
	{
		auto templates = std::move(content.powers.templates);
		content.powers = BindSpecialPowers(*base.sources.commandButtons, *creationLists, step);
		content.powers.templates = std::move(templates);
		content.creation = BindCreationLists(*creationLists, step);
	}
	if (specialPowers != nullptr)
		content.powers.templates = BindSpecialPowerTemplates(*specialPowers, step);
	if (const engine::config::Document *commandSets = overlay(base.sources.commandSets, "CommandSet"); commandSets != nullptr && base.sources.commandButtons != nullptr)
	{
		content.buildLists = BindBuildLists(*commandSets, *base.sources.commandButtons);
		content.researchLists = BindResearchLists(*commandSets, *base.sources.commandButtons);
		content.commands = BindCommandCatalog(*commandSets, *base.sources.commandButtons);
	}
	if (const engine::config::Document *sciences = overlay(base.sources.sciences, "Science"))
	{
		content.scienceInfo = BindSciences(*sciences);
		content.sciences.clear();
		for (const ScienceInfo &science : content.scienceInfo)
			content.sciences.push_back(science.name);
	}
	if (const engine::config::Document *upgrades = overlay(base.sources.upgrades, "Upgrade"))
		content.upgrades = BuildUpgradeCatalog(*upgrades, step.TicksPerSecond());
	if (const engine::config::Document *aiData = overlay(base.sources.aiData, "AIData"))
		content.aiData = BindAiData(*aiData, context);
	if (const engine::config::Document *crates = overlay(base.sources.crates, "CrateData"))
		content.crates = BindCrateTemplates(*crates);

	// What the changed objects, and those moving on a changed locomotor, give.
	if (!changedLocomotors.empty())
		for (const auto &[name, object] : content.objects)
			for (const engine::config::Node *set : object.locomotorSets)
				if (set != nullptr && std::ranges::any_of(set->values, [&](std::string_view value) { return changedLocomotors.contains(value); }))
				{
					changedObjects.push_back(name);
					break;
				}
	if (changedObjects.empty() && creationLists == nullptr)
		return content;
	ModelRigs rigs(loader.Files());
	std::ranges::sort(changedObjects);
	changedObjects.erase(std::unique(changedObjects.begin(), changedObjects.end()), changedObjects.end());
	for (const std::string &name : changedObjects)
		if (const ObjectDefinition *object = content.objects.Find(name))
		{
			ForgetObjectTables(content, name);
			DeriveObjectTables(content, name, *object, rigs, step, context);
		}
	DeriveVisibleRunBones(content, rigs);
	return content;
}
}
