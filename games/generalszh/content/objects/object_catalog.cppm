export module games.generalszh.content.objects.object_catalog;
import std;

export import games.generalszh.content.objects.object_definition;
import games.generalszh.content.objects.module_interfaces;

namespace generalszh::content::detail
{
using engine::config::BindContext;
using engine::config::EnumName;
using engine::config::Node;
using engine::config::Schema;

constexpr std::array<EnumName<GeometryShape>, 3> GeometryShapes{{
	{"SPHERE", GeometryShape::Sphere}, {"CYLINDER", GeometryShape::Cylinder}, {"BOX", GeometryShape::Box}}};

struct ModuleSlotName
{
	std::string_view key;
	ModuleSlot slot;
};
constexpr std::array<ModuleSlotName, 4> ModuleSlots{{
	{"Behavior", ModuleSlot::Behavior}, {"Body", ModuleSlot::Body}, {"Draw", ModuleSlot::Draw}, {"ClientUpdate", ModuleSlot::ClientUpdate}}};

// Module-line context while binding one definition (ThingTemplate::m_moduleParsingMode, and for ReplaceModule the
// module being replaced: m_moduleBeingReplacedName / m_moduleBeingReplacedTag).
struct ModuleMode
{
	bool inheritable{false};
	bool overrideableByLikeKind{false};
	std::string_view replacedType;
	std::string_view replacedTag;
	bool clearCopied{true}; // false in an override load (INI_LOAD_CREATE_OVERRIDES: parseModuleName clears nothing)
};

// How a definition's fields are being read (ThingTemplate::m_moduleParsingMode and the INI's load type): an override
// load (a map's map.ini) takes modules only inside AddModule / ReplaceModule; those blocks, InheritableModule and
// OverrideableByLikeKind read the whole field table again in their mode, and may not nest.
struct ParseMode
{
	enum class Modules : std::uint8_t
	{
		Normal,
		AddRemoveReplace,
		Inheritable,
		OverrideableByLikeKind,
	};
	bool overrideLoad{false};
	Modules modules{Modules::Normal};
	std::string replacedType;
	std::string replacedTag;
};

// The slots that share one ModuleInfo (Body lines go into the behaviour modules: parseModuleName's type 999).
inline bool SameModuleInfo(ModuleSlot a, ModuleSlot b) noexcept
{
	const auto info = [](ModuleSlot slot) { return slot == ModuleSlot::Body ? ModuleSlot::Behavior : slot; };
	return info(a) == info(b);
}

bool AddModule(const Node &node, ObjectDefinition &out, BindContext &context, ModuleSlot slot, ModuleMode mode)
{
	if (!node.block || node.values.size() < 2)
	{
		context.diagnostics.Error(node.location, "'" + std::string(node.key) + "' needs a module type and tag and must end with End");
		return false;
	}
	const std::string_view declared = node.values[0];
	const std::string_view tag = node.values[1];
	// ModuleFactory::findModuleInterfaceMask: what the module type supports (a type the original does not register
	// would fail to load there; here it is kept, composed by its slot).
	const ModuleInterfaces *interfaces = FindModuleInterfaces(declared);
	if (interfaces == nullptr)
		context.diagnostics.Warning(node.location, "'" + std::string(declared) + "' is not a module type the original registers");
	else
	{
		// parseModuleName: a Body line takes only body modules, every other line none (INI_INVALID_DATA).
		const bool body = (interfaces->mask & module_interface::Body) != 0;
		if ((slot == ModuleSlot::Body) != body)
		{
			context.diagnostics.Error(node.location, slot == ModuleSlot::Body ? "only a body module may be a Body" : "a body module must be a Body");
			return false;
		}
	}
	// parseModuleName within ReplaceModule: the replacement must be of the replaced module's type, under a new tag.
	if (!mode.replacedType.empty() && mode.replacedType != declared)
	{
		context.diagnostics.Error(node.location, "ReplaceModule must replace a " + std::string(mode.replacedType) + " with the same type, not " + std::string(declared));
		return false;
	}
	if (!mode.replacedTag.empty() && mode.replacedTag == tag)
	{
		context.diagnostics.Error(node.location, "ReplaceModule must give the replacement a new tag, not " + std::string(tag));
		return false;
	}
	// ModuleInfo::clearCopiedFromDefaultEntries (on every module info, with the new module's interfaces): a copied
	// (default or reskin) module sharing an interface with it goes, unless inheritable - then only the default auto heal
	// of an object not IsTrainable (as it stands so far) - or overrideable by like kind - then (the default StealthUpdate
	// the GPS scrambler grants) only when one of its type is declared, or the object, as its kinds stand so far, is one
	// the scrambler never cloaks (aircraft, shrubbery, structures, boats, bridges and the like) or none of its
	// candidates (SCORE, VEHICLE, INFANTRY, PORTABLE_STRUCTURE). An unregistered type shares interfaces by slot.
	const auto likeKindGoes = [&](const ModuleEntry &entry) {
		static constexpr std::array<KindOfName, 16> immune{"AIRCRAFT", "SHRUBBERY", "OPTIMIZED_TREE", "STRUCTURE", "DRAWABLE_ONLY", "MOB_NEXUS",
			"IGNORED_IN_GUI", "CLEARED_BY_BUILD", "DEFENSIVE_WALL", "BALLISTIC_MISSILE", "SUPPLY_SOURCE", "BOAT", "INERT", "BRIDGE",
			"LANDMARK_BRIDGE", "BRIDGE_TOWER"};
		static constexpr std::array<KindOfName, 4> candidates{"SCORE", "VEHICLE", "INFANTRY", "PORTABLE_STRUCTURE"};
		const auto any = [&](const auto &kinds) { return std::ranges::any_of(kinds, [&](KindOfName kind) { return HasKindOf(out.kinds, kind.bit); }); };
		return entry.type == declared || any(immune) || !any(candidates);
	};
	const auto sharesInterface = [&](const ModuleEntry &entry) {
		const ModuleInterfaces *theirs = FindModuleInterfaces(entry.type);
		if (interfaces == nullptr || theirs == nullptr)
			return entry.slot == slot;
		return (theirs->mask & interfaces->mask) != 0;
	};
	std::erase_if(out.modules, [&](const ModuleEntry &entry) {
		if (!mode.clearCopied || !entry.copied || !sharesInterface(entry))
			return false;
		if (entry.inheritable)
			return entry.tag == "ModuleTag_DefaultAutoHealBehavior" && !out.trainable;
		return !entry.overrideableByLikeKind || likeKindGoes(entry);
	});
	// ModuleInfo::clearAiModuleInfo: a module whose data is an AI's (AIUpdateModuleData) drops every AI module of its
	// module info first, copied or declared.
	if (interfaces != nullptr && interfaces->ai)
		std::erase_if(out.modules, [&](const ModuleEntry &entry) {
			const ModuleInterfaces *theirs = FindModuleInterfaces(entry.type);
			return theirs != nullptr && theirs->ai && SameModuleInfo(entry.slot, slot);
		});
	ModuleEntry entry;
	entry.slot = slot;
	entry.type = std::string(node.values[0]);
	entry.tag = std::string(node.values[1]);
	entry.block = &node;
	entry.inheritable = mode.inheritable;
	entry.overrideableByLikeKind = mode.overrideableByLikeKind;
	out.modules.push_back(std::move(entry));
	return true;
}

// A module line (Behavior, Body, Draw, ClientUpdate) as ThingTemplate::parseModuleName reads it in `mode`: in an override
// load only within AddModule / ReplaceModule ("You must use AddModule to add modules in override INI files").
bool ModuleLine(const Node &node, ObjectDefinition &out, BindContext &context, ModuleSlot slot, const ParseMode &mode)
{
	if (mode.overrideLoad && mode.modules != ParseMode::Modules::AddRemoveReplace)
	{
		context.diagnostics.Error(node.location, "an override must add modules with AddModule");
		return false;
	}
	ModuleMode modules;
	modules.inheritable = mode.modules == ParseMode::Modules::Inheritable;
	modules.overrideableByLikeKind = mode.modules == ParseMode::Modules::OverrideableByLikeKind;
	modules.replacedType = mode.replacedType;
	modules.replacedTag = mode.replacedTag;
	modules.clearCopied = !mode.overrideLoad;
	return AddModule(node, out, context, slot, modules);
}

// The object's sound events (ThingTemplate's audio array and per-unit sounds).
constexpr std::array<std::string_view, 36> SoundRoles{"VoiceSelect", "VoiceGroupSelect", "VoiceMove", "VoiceAttack", "VoiceEnter",
	"VoiceFear", "VoiceSelectElite", "VoiceCreated", "VoiceTaskUnable", "VoiceTaskComplete", "VoiceMeetEnemy", "VoiceGarrison",
	"VoiceSurrender", "VoiceDefect", "VoiceAttackSpecial", "VoiceAttackAir", "VoiceGuard", "SoundMoveStart", "SoundMoveStartDamaged",
	"SoundMoveLoop", "SoundMoveLoopDamaged", "SoundAmbient", "SoundAmbientDamaged", "SoundAmbientReallyDamaged", "SoundAmbientRubble",
	"SoundStealthOn", "SoundStealthOff", "SoundCreated", "SoundOnDamaged", "SoundOnReallyDamaged", "SoundEnter", "SoundExit",
	"SoundPromotedVeteran", "SoundPromotedElite", "SoundPromotedHero", "SoundFallingFromPlane"};

void AddSounds(Schema<ObjectDefinition> &schema)
{
	for (const std::string_view role : SoundRoles)
		schema.On(std::string(role), [role](const Node &node, ObjectDefinition &out, BindContext &) {
			// "NoSound" (and an empty value) clears an inherited sound.
			const std::string_view name = node.Value();
			if (name.empty() || name == "NoSound")
				out.sounds.erase(std::string(role));
			else
				out.sounds.insert_or_assign(std::string(role), std::string(name));
		});
	schema.On("UnitSpecificSounds", [](const Node &node, ObjectDefinition &out, BindContext &) {
		for (const Node &entry : node.children)
		{
			out.sounds.insert_or_assign(std::string(entry.key), std::string(entry.Value()));
			out.unitSounds.insert_or_assign(std::string(entry.key), std::string(entry.Value()));
		}
	});
	schema.On("UnitSpecificFX", [](const Node &node, ObjectDefinition &out, BindContext &) {
		for (const Node &entry : node.children)
			out.unitFx.insert_or_assign(std::string(entry.key), std::string(entry.Value()));
	});
}

// Geometry, GeometryMajorRadius, GeometryMinorRadius, GeometryHeight, GeometryIsSmall (GeometryInfo's parsers).
void AddGeometry(Schema<ObjectDefinition> &schema)
{
	schema.On("Geometry", [](const Node &node, ObjectDefinition &out, BindContext &context) {
			if (const auto shape = engine::config::ReadEnum<GeometryShape>(node, context, std::span(GeometryShapes)))
				out.geometry.shape = *shape;
		})
		.On("GeometryMajorRadius", [](const Node &node, ObjectDefinition &out, BindContext &context) {
			if (const auto value = engine::config::ReadFixed(node, context)) out.geometry.majorRadius = *value;
		})
		.On("GeometryMinorRadius", [](const Node &node, ObjectDefinition &out, BindContext &context) {
			if (const auto value = engine::config::ReadFixed(node, context)) out.geometry.minorRadius = *value;
		})
		.On("GeometryHeight", [](const Node &node, ObjectDefinition &out, BindContext &context) {
			if (const auto value = engine::config::ReadFixed(node, context)) out.geometry.height = *value;
		})
		.On("GeometryIsSmall", [](const Node &node, ObjectDefinition &out, BindContext &context) {
			if (const auto value = engine::config::ReadBool(node, context)) out.geometry.small = *value;
		});
}

// MaxSimultaneousOfType (ThingTemplate::parseMaxSimultaneous: a number, or DeterminedBySuperweaponRestriction) and
// MaxSimultaneousLinkKey.
void AddMaxSimultaneous(Schema<ObjectDefinition> &schema)
{
	schema.String("MaxSimultaneousLinkKey", &ObjectDefinition::maxSimultaneousLinkKey)
		.On("MaxSimultaneousOfType", [](const Node &node, ObjectDefinition &out, BindContext &) {
			out.maxSimultaneousBySuperweaponRestriction = std::ranges::equal(node.Value(), std::string_view{"DeterminedBySuperweaponRestriction"}, [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
			out.maxSimultaneousOfType = out.maxSimultaneousBySuperweaponRestriction ? 0u
				: static_cast<std::uint32_t>(std::clamp<std::int64_t>(engine::config::values::ParseInt(node.Value()).value_or(0), 0, 65535));
		});
}

void AddCommon(Schema<ObjectDefinition> &schema)
{
	AddSounds(schema);
	schema.String("DisplayName", &ObjectDefinition::displayName)
		.String("ButtonImage", &ObjectDefinition::buttonImage)
		.String("SelectPortrait", &ObjectDefinition::selectPortrait)
		.On("UpgradeCameo1", [](const Node &node, ObjectDefinition &out, BindContext &) { out.upgradeCameos[0] = std::string(node.Value()); })
		.On("UpgradeCameo2", [](const Node &node, ObjectDefinition &out, BindContext &) { out.upgradeCameos[1] = std::string(node.Value()); })
		.On("UpgradeCameo3", [](const Node &node, ObjectDefinition &out, BindContext &) { out.upgradeCameos[2] = std::string(node.Value()); })
		.On("UpgradeCameo4", [](const Node &node, ObjectDefinition &out, BindContext &) { out.upgradeCameos[3] = std::string(node.Value()); })
		.On("UpgradeCameo5", [](const Node &node, ObjectDefinition &out, BindContext &) { out.upgradeCameos[4] = std::string(node.Value()); })
		.String("EditorSorting", &ObjectDefinition::editorSorting)
		.Fixed("VisionRange", &ObjectDefinition::visionRange)
		.Fixed("ShroudClearingRange", &ObjectDefinition::shroudClearingRange)
		.Fixed("ShroudRevealToAllRange", &ObjectDefinition::shroudRevealToAllRange)
		.Integer("BuildCost", &ObjectDefinition::buildCost)
		.Integer("RefundValue", &ObjectDefinition::refundValue)
		.Integer("EnergyProduction", &ObjectDefinition::energyProduction)
		.Integer("EnergyBonus", &ObjectDefinition::energyBonus)
		.StringList("BuildVariations", &ObjectDefinition::buildVariations)
		.Fixed("BuildTime", &ObjectDefinition::buildTimeSeconds)
		.Fixed("PlacementViewAngle", &ObjectDefinition::placementViewAngleDegrees)
		.Integer("TransportSlotCount", &ObjectDefinition::transportSlots)
		.On("ExperienceValue", [](const Node &node, ObjectDefinition &out, BindContext &) {
			// ThingTemplate::parseIntList: one per level; missing ones stay zero.
			for (std::size_t level = 0; level < out.experienceValue.size() && level < node.values.size(); ++level)
				out.experienceValue[level] = static_cast<std::int32_t>(engine::config::values::ParseInt(node.values[level]).value_or(0));
		})
		.On("SkillPointValue", [](const Node &node, ObjectDefinition &out, BindContext &) {
			// ThingTemplate::parseIntList; "USE_EXP_VALUE_FOR_SKILL_VALUE" is -999.
			for (std::size_t level = 0; level < out.skillPointValue.size() && level < node.values.size(); ++level)
				out.skillPointValue[level] = static_cast<std::int32_t>(engine::config::values::ParseInt(node.values[level]).value_or(-999));
		})
		.On("ExperienceRequired", [](const Node &node, ObjectDefinition &out, BindContext &) {
			for (std::size_t level = 0; level < out.experienceRequired.size() && level < node.values.size(); ++level)
				out.experienceRequired[level] = static_cast<std::int32_t>(engine::config::values::ParseInt(node.values[level]).value_or(0));
		})
		.On("IsTrainable", [](const Node &node, ObjectDefinition &out, BindContext &bind) {
			out.trainable = engine::config::ReadBool(node, bind).value_or(false);
		})
		.On("IsBridge", [](const Node &node, ObjectDefinition &out, BindContext &bind) {
			out.isBridge = engine::config::ReadBool(node, bind).value_or(false);
		})
		.Integer("CrusherLevel", &ObjectDefinition::crusherLevel)
		.Integer("CrushableLevel", &ObjectDefinition::crushableLevel)
		.On("Prerequisites", [](const Node &node, ObjectDefinition &out, BindContext &) {
			out.prerequisiteObjects.clear();
			out.prerequisiteSciences.clear();
			out.prerequisiteOrder.clear();
			for (const Node &line : node.children)
			{
				if (line.key == "Object" && !line.values.empty())
				{
					out.prerequisiteObjects.emplace_back(line.values.begin(), line.values.end());
					out.prerequisiteOrder.push_back('O');
				}
				else if (line.key == "Science")
				{
					out.prerequisiteSciences.insert(out.prerequisiteSciences.end(), line.values.begin(), line.values.end());
					out.prerequisiteOrder.append(line.values.size(), 'S');
				}
			}
		})
		// ThingTemplate::parseArmorTemplateSet / parseWeaponTemplateSet: the object's first own set drops every set copied
		// from DefaultThingTemplate or the reskin source (m_armorCopiedFromDefault / m_weaponsCopiedFromDefault).
		.On("ArmorSet", [](const Node &node, ObjectDefinition &out, BindContext &) {
			if (out.armorCopied)
			{
				out.armorCopied = false;
				out.armorSets.clear();
			}
			out.armorSets.push_back(&node);
		})
		.On("WeaponSet", [](const Node &node, ObjectDefinition &out, BindContext &) {
			if (out.weaponsCopied)
			{
				out.weaponsCopied = false;
				out.weaponSets.clear();
			}
			out.weaponSets.push_back(&node);
		});
	AddGeometry(schema);
}

void NestedBlock(const Node &node, ObjectDefinition &out, BindContext &context, const ParseMode &outer, ParseMode::Modules modules,
	std::string replacedType, std::string replacedTag);

// ThingTemplate::s_objectFieldParseTable, read in `mode`.
Schema<ObjectDefinition> ObjectSchema(const ParseMode &mode)
{
	Schema<ObjectDefinition> schema;
	AddCommon(schema);
	for (const ModuleSlotName &slot : ModuleSlots)
		schema.On(std::string(slot.key), [slot = slot.slot, mode](const Node &node, ObjectDefinition &out, BindContext &context) {
			ModuleLine(node, out, context, slot, mode);
		});
	AddMaxSimultaneous(schema);
	schema.String("Side", &ObjectDefinition::side)
		.String("CommandSet", &ObjectDefinition::commandSet)
		.String("RadarPriority", &ObjectDefinition::radarPriority)
		.On("Shadow", [](const Node &node, ObjectDefinition &out, BindContext &) {
			constexpr std::array<std::string_view, 7> names{"SHADOW_DECAL", "SHADOW_VOLUME", "SHADOW_PROJECTION", "SHADOW_DYNAMIC_PROJECTION",
				"SHADOW_DIRECTIONAL_PROJECTION", "SHADOW_ALPHA_DECAL", "SHADOW_ADDITIVE_DECAL"};
			out.shadow = 0;
			for (const std::string_view token : node.values)
				for (std::size_t bit = 0; bit < names.size(); ++bit)
					if (std::ranges::equal(token, names[bit], [](char a, char b) { return std::toupper(static_cast<unsigned char>(a)) == std::toupper(static_cast<unsigned char>(b)); }))
						out.shadow = static_cast<std::uint8_t>(out.shadow | (1u << bit));
		})
		.On("Buildable", [](const Node &node, ObjectDefinition &out, BindContext &) {
			constexpr std::array<std::string_view, 4> names{"Yes", "Ignore_Prerequisites", "No", "Only_By_AI"};
			for (std::size_t index = 0; index < names.size(); ++index)
				if (std::ranges::equal(node.Value(), names[index], [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); }))
					out.buildable = static_cast<ObjectDefinition::Buildable>(index);
		})
		.Fixed("Scale", &ObjectDefinition::scale)
		.Fixed("InstanceScaleFuzziness", &ObjectDefinition::instanceScaleFuzziness)
		.Fixed("FenceWidth", &ObjectDefinition::fenceWidth)
		.Ignore("FenceXOffset") // read by nothing (ThingTemplate::getFenceXOffset has no caller)
		.Fixed("FactoryExitWidth", &ObjectDefinition::factoryExitWidth)
		.Fixed("FactoryExtraBibWidth", &ObjectDefinition::factoryExtraBibWidth)
		.Integer("ThreatValue", &ObjectDefinition::threat)
		.Fixed("StructureRubbleHeight", &ObjectDefinition::structureRubbleHeight)
		.On("KindOf", [](const Node &node, ObjectDefinition &out, BindContext &context) {
			if (const auto mask = engine::config::ReadFlags<KindOfWords>(node, context, std::span(KindOfNames), out.kinds))
				out.kinds = *mask;
		})
		.On("Locomotor", [](const Node &node, ObjectDefinition &out, BindContext &) { out.locomotorSets.push_back(&node); })
		// ThingTemplate::parseInheritableModule / OverrideableByLikeKind / parseAddModule: the block read with the whole
		// field table in that module mode (not within another mode: INI_INVALID_DATA).
		.On("InheritableModule", [mode](const Node &node, ObjectDefinition &out, BindContext &context) {
			NestedBlock(node, out, context, mode, ParseMode::Modules::Inheritable, {}, {});
		})
		.On("OverrideableByLikeKind", [mode](const Node &node, ObjectDefinition &out, BindContext &context) {
			NestedBlock(node, out, context, mode, ParseMode::Modules::OverrideableByLikeKind, {}, {});
		})
		.On("AddModule", [mode](const Node &node, ObjectDefinition &out, BindContext &context) {
			NestedBlock(node, out, context, mode, ParseMode::Modules::AddRemoveReplace, {}, {});
		})
		// ThingTemplate::parseRemoveModule: every module of that tag goes (removeModuleInfo); none: INI_INVALID_DATA.
		.On("RemoveModule", [mode](const Node &node, ObjectDefinition &out, BindContext &context) {
			if (mode.modules != ParseMode::Modules::Normal)
			{
				context.diagnostics.Error(node.location, "RemoveModule may not be within another module block");
				return;
			}
			const std::string_view tag = node.Value();
			if (std::erase_if(out.modules, [&](const ModuleEntry &entry) { return entry.tag == tag; }) == 0)
				context.diagnostics.Error(node.location, "RemoveModule: no module tagged '" + std::string(tag) + "'");
		})
		// ThingTemplate::parseReplaceModule: the tagged module goes (none: INI_INVALID_DATA), then the block is read in its
		// place, each of its modules of the replaced module's type under a new tag (parseModuleName).
		.On("ReplaceModule", [mode](const Node &node, ObjectDefinition &out, BindContext &context) {
			if (mode.modules != ParseMode::Modules::Normal)
			{
				context.diagnostics.Error(node.location, "ReplaceModule may not be within another module block");
				return;
			}
			const std::string tag(node.Value());
			const auto found = std::find_if(out.modules.begin(), out.modules.end(), [&](const ModuleEntry &entry) { return entry.tag == tag; });
			if (found == out.modules.end())
			{
				context.diagnostics.Error(node.location, "ReplaceModule: no module tagged '" + tag + "'");
				return;
			}
			const std::string replacedType = found->type;
			std::erase_if(out.modules, [&](const ModuleEntry &entry) { return entry.tag == tag; });
			NestedBlock(node, out, context, mode, ParseMode::Modules::AddRemoveReplace, replacedType, tag);
		});
	return schema;
}

void NestedBlock(const Node &node, ObjectDefinition &out, BindContext &context, const ParseMode &outer, ParseMode::Modules modules,
	std::string replacedType, std::string replacedTag)
{
	if (outer.modules != ParseMode::Modules::Normal)
	{
		context.diagnostics.Error(node.location, "'" + std::string(node.key) + "' may not be within another module block");
		return;
	}
	ParseMode nested = outer;
	nested.modules = modules;
	nested.replacedType = std::move(replacedType);
	nested.replacedTag = std::move(replacedTag);
	ObjectSchema(nested).Bind(node, out, context);
}

// ThingTemplate::s_objectReskinFieldParseTable: a reskin may change only its Draw modules, its geometry, its fence
// (FenceWidth, FenceXOffset) and MaxSimultaneousOfType / MaxSimultaneousLinkKey.
Schema<ObjectDefinition> ReskinSchema(const ParseMode &mode)
{
	Schema<ObjectDefinition> schema;
	schema.On("Draw", [mode](const Node &node, ObjectDefinition &out, BindContext &context) { ModuleLine(node, out, context, ModuleSlot::Draw, mode); })
		.Fixed("FenceWidth", &ObjectDefinition::fenceWidth)
		.Ignore("FenceXOffset");
	AddGeometry(schema);
	AddMaxSimultaneous(schema);
	return schema;
}
}

export namespace generalszh::content
{
// Builds every Object and ObjectReskin in `documents` (in load order: the
// original accepts object blocks in any content set, e.g. crates), with the
// original composition rules: objects start as a copy of the
// DefaultThingTemplate object; a reskin starts as a copy of its source (which
// must be defined earlier) and may only change the reskin field set.
engine::config::DefinitionTable<ObjectDefinition> BuildObjectCatalog(std::span<const engine::config::Document *const> documents,
	engine::config::BindContext &context)
{
	using engine::config::Node;
	engine::config::DefinitionTable<ObjectDefinition> objects;
	const auto objectSchema = detail::ObjectSchema({});
	const auto reskinSchema = detail::ReskinSchema({});
	const auto copyFrom = [](const ObjectDefinition &source) {
		ObjectDefinition copy = source;
		// ThingTemplate::setCopiedFromDefault.
		for (ModuleEntry &module : copy.modules)
			module.copied = true;
		copy.armorCopied = true;
		copy.weaponsCopied = true;
		return copy;
	};
	for (const engine::config::Document *document : documents)
	for (const Node &root : document->Roots())
	{
		const bool reskin = root.key == "ObjectReskin";
		if (root.key != "Object" && !reskin)
			continue;
		const std::string name(root.Value(0));
		if (name.empty() || (reskin && root.Value(1).empty()))
		{
			context.diagnostics.Error(root.location, "'" + std::string(root.key) + "' needs a name" + (reskin ? " and a source" : ""));
			continue;
		}
		if (objects.Contains(name))
		{
			context.diagnostics.Error(root.location, "'" + name + "' is already defined");
			continue;
		}
		ObjectDefinition definition;
		if (reskin)
		{
			const ObjectDefinition *source = objects.Find(root.Value(1));
			if (source == nullptr)
			{
				context.diagnostics.Error(root.location, "ObjectReskin '" + name + "': source '" + std::string(root.Value(1)) + "' must be defined first");
				continue;
			}
			definition = copyFrom(*source);
			definition.reskinnedFrom = std::string(root.Value(1));
		}
		else if (const ObjectDefinition *defaults = objects.Find("DefaultThingTemplate"))
		{
			definition = copyFrom(*defaults);
		}
		definition.name = name;
		definition.definedAt.push_back(root.location);
		(reskin ? reskinSchema : objectSchema).Bind(root, definition, context);
		objects.Define(name) = std::move(definition);
	}
	// ThingTemplate::resolveNames: a build facility is any object another names in a Prerequisites Object line (each of
	// its alternatives: getAllPossibleBuildFacilityTemplates), and every COMMANDCENTER ("considered factories").
	std::vector<std::string> facilities;
	for (const auto &[name, definition] : objects)
	{
		for (const auto &alternatives : definition.prerequisiteObjects)
			facilities.insert(facilities.end(), alternatives.begin(), alternatives.end());
		if (definition.Is("COMMANDCENTER"))
			facilities.push_back(name);
	}
	for (const std::string &name : facilities)
		if (ObjectDefinition *facility = objects.Find(name))
			facility->buildFacility = true;
	return objects;
}

inline engine::config::DefinitionTable<ObjectDefinition> BuildObjectCatalog(const engine::config::Document &document,
	engine::config::BindContext &context)
{
	const engine::config::Document *documents[] = {&document};
	return BuildObjectCatalog(documents, context);
}

// A map's map.ini Object / ObjectReskin blocks over `objects` (GameLogic::startNewGame loads map.ini with
// INI_LOAD_CREATE_OVERRIDES; ThingFactory::parseObjectDefinition): a known object gets an override, a copy of what it is
// so far (ThingFactory::newOverride: copied from its final override, setCopiedFromDefault) read in override mode -
// modules only within AddModule / ReplaceModule, no copied module cleared by a new one (parseModuleName), its first own
// ArmorSet / WeaponSet replacing the sets; an unknown one starts from DefaultThingTemplate (newTemplate) and is read the
// same way; a reskin copies its source and reads the reskin fields. Each one's Prerequisites name build facilities again
// (resolveNames). The names it changed or added, in order.
std::vector<std::string> ApplyObjectOverrides(engine::config::DefinitionTable<ObjectDefinition> &objects, const engine::config::Document &mapIni,
	engine::config::BindContext &context)
{
	using engine::config::Node;
	detail::ParseMode overrideMode;
	overrideMode.overrideLoad = true;
	const auto objectSchema = detail::ObjectSchema(overrideMode);
	const auto reskinSchema = detail::ReskinSchema(overrideMode);
	const auto copyFrom = [](const ObjectDefinition &source) {
		ObjectDefinition copy = source;
		for (ModuleEntry &module : copy.modules)
			module.copied = true;
		copy.armorCopied = true;
		copy.weaponsCopied = true;
		return copy;
	};
	std::vector<std::string> changed;
	for (const Node &root : mapIni.Roots())
	{
		const bool reskin = root.key == "ObjectReskin";
		if (root.key != "Object" && !reskin)
			continue;
		const std::string name(root.Value(0));
		if (name.empty() || (reskin && root.Value(1).empty()))
		{
			context.diagnostics.Error(root.location, "'" + std::string(root.key) + "' needs a name" + (reskin ? " and a source" : ""));
			continue;
		}
		ObjectDefinition definition;
		if (reskin)
		{
			const ObjectDefinition *source = objects.Find(root.Value(1));
			if (source == nullptr)
			{
				context.diagnostics.Error(root.location, "ObjectReskin '" + name + "': source '" + std::string(root.Value(1)) + "' must be defined first");
				continue;
			}
			definition = copyFrom(*source);
			definition.reskinnedFrom = std::string(root.Value(1));
		}
		else if (const ObjectDefinition *existing = objects.Find(name))
			definition = copyFrom(*existing);
		else if (const ObjectDefinition *defaults = objects.Find("DefaultThingTemplate"))
			definition = copyFrom(*defaults);
		definition.name = name;
		definition.definedAt.push_back(root.location);
		(reskin ? reskinSchema : objectSchema).Bind(root, definition, context);
		// ThingTemplate::resolveNames for the override: what its prerequisites name are build facilities; a command centre
		// is one.
		for (const auto &alternatives : definition.prerequisiteObjects)
			for (const std::string &facility : alternatives)
				if (ObjectDefinition *found = objects.Find(facility))
					found->buildFacility = true;
		if (definition.Is("COMMANDCENTER"))
			definition.buildFacility = true;
		objects.Define(name) = std::move(definition);
		if (std::find(changed.begin(), changed.end(), name) == changed.end())
			changed.push_back(name);
	}
	return changed;
}
}
