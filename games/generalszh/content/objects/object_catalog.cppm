export module games.generalszh.content.objects.object_catalog;
import std;

export import games.generalszh.content.objects.object_definition;

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

// Module-line context while binding one definition.
struct ModuleMode
{
	bool inheritable{false};
	bool overrideableByLikeKind{false};
};

bool AddModule(const Node &node, ObjectDefinition &out, BindContext &context, ModuleSlot slot, ModuleMode mode)
{
	if (!node.block || node.values.size() < 2)
	{
		context.diagnostics.Error(node.location, "'" + std::string(node.key) + "' needs a module type and tag and must end with End");
		return false;
	}
	// A module declared by this object replaces copied (default/reskin)
	// modules of the same slot unless those were declared inheritable.
	// The original narrows this by module interface category; that rule is
	// applied once module types carry their categories (see the ledger).
	std::erase_if(out.modules, [&](const ModuleEntry &entry) { return entry.copied && !entry.inheritable && entry.slot == slot; });
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

void AddModulesIn(const Node &group, ObjectDefinition &out, BindContext &context, ModuleMode mode)
{
	for (const Node &child : group.children)
	{
		const auto slot = std::find_if(ModuleSlots.begin(), ModuleSlots.end(), [&](const ModuleSlotName &entry) { return entry.key == child.key; });
		if (slot == ModuleSlots.end())
			context.diagnostics.Warning(child.location, "'" + std::string(child.key) + "' is not a module line in '" + std::string(group.key) + "'");
		else
			AddModule(child, out, context, slot->slot, mode);
	}
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
			out.sounds.insert_or_assign(std::string(entry.key), std::string(entry.Value()));
	});
}

void AddCommon(Schema<ObjectDefinition> &schema)
{
	AddSounds(schema);
	schema.String("DisplayName", &ObjectDefinition::displayName)
		.String("ButtonImage", &ObjectDefinition::buttonImage)
		.String("SelectPortrait", &ObjectDefinition::selectPortrait)
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
			for (const Node &line : node.children)
			{
				if (line.key == "Object" && !line.values.empty())
					out.prerequisiteObjects.emplace_back(line.values.begin(), line.values.end());
				else if (line.key == "Science")
					out.prerequisiteSciences.insert(out.prerequisiteSciences.end(), line.values.begin(), line.values.end());
			}
		})
		.On("ArmorSet", [](const Node &node, ObjectDefinition &out, BindContext &) { out.armorSets.push_back(&node); })
		.On("WeaponSet", [](const Node &node, ObjectDefinition &out, BindContext &) { out.weaponSets.push_back(&node); })
		.On("Geometry", [](const Node &node, ObjectDefinition &out, BindContext &context) {
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
	for (const ModuleSlotName &slot : ModuleSlots)
		schema.On(std::string(slot.key), [slot = slot.slot](const Node &node, ObjectDefinition &out, BindContext &context) {
			AddModule(node, out, context, slot, {});
		});
}

Schema<ObjectDefinition> ObjectSchema()
{
	Schema<ObjectDefinition> schema;
	AddCommon(schema);
	schema.String("Side", &ObjectDefinition::side)
		.String("CommandSet", &ObjectDefinition::commandSet)
		.String("RadarPriority", &ObjectDefinition::radarPriority)
		.String("MaxSimultaneousLinkKey", &ObjectDefinition::maxSimultaneousLinkKey)
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
		.On("MaxSimultaneousOfType", [](const Node &node, ObjectDefinition &out, BindContext &) {
			out.maxSimultaneousBySuperweaponRestriction = std::ranges::equal(node.Value(), std::string_view{"DeterminedBySuperweaponRestriction"}, [](char a, char b) { return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b)); });
			out.maxSimultaneousOfType = out.maxSimultaneousBySuperweaponRestriction ? 0u
				: static_cast<std::uint32_t>(std::clamp<std::int64_t>(engine::config::values::ParseInt(node.Value()).value_or(0), 0, 65535));
		})
		.Fixed("Scale", &ObjectDefinition::scale)
		.Fixed("InstanceScaleFuzziness", &ObjectDefinition::instanceScaleFuzziness)
		.Integer("ThreatValue", &ObjectDefinition::threat)
		.Fixed("StructureRubbleHeight", &ObjectDefinition::structureRubbleHeight)
		.On("KindOf", [](const Node &node, ObjectDefinition &out, BindContext &context) {
			if (const auto mask = engine::config::ReadFlags<KindOfWords>(node, context, std::span(KindOfNames), out.kinds))
				out.kinds = *mask;
		})
		.On("Locomotor", [](const Node &node, ObjectDefinition &out, BindContext &) { out.locomotorSets.push_back(&node); })
		.On("InheritableModule", [](const Node &node, ObjectDefinition &out, BindContext &context) {
			AddModulesIn(node, out, context, {true, false});
		})
		.On("OverrideableByLikeKind", [](const Node &node, ObjectDefinition &out, BindContext &context) {
			AddModulesIn(node, out, context, {false, true});
		})
		.On("AddModule", [](const Node &node, ObjectDefinition &out, BindContext &context) { AddModulesIn(node, out, context, {}); })
		.On("RemoveModule", [](const Node &node, ObjectDefinition &out, BindContext &context) {
			const std::string_view tag = node.Value();
			if (std::erase_if(out.modules, [&](const ModuleEntry &entry) { return entry.tag == tag; }) == 0)
				context.diagnostics.Warning(node.location, "RemoveModule: no module tagged '" + std::string(tag) + "'");
		})
		.On("ReplaceModule", [](const Node &node, ObjectDefinition &out, BindContext &context) {
			const std::string_view tag = node.Value();
			const auto found = std::find_if(out.modules.begin(), out.modules.end(), [&](const ModuleEntry &entry) { return entry.tag == tag; });
			if (found == out.modules.end() || node.children.size() != 1)
			{
				context.diagnostics.Warning(node.location, "ReplaceModule: expected one module replacing tag '" + std::string(tag) + "'");
				return;
			}
			const Node &replacement = node.children.front();
			const ModuleSlot slot = found->slot;
			out.modules.erase(found);
			AddModule(replacement, out, context, slot, {});
		});
	return schema;
}

// ObjectReskin accepts only the fields that change how an object looks or
// what it costs, as in the original reskin table.
Schema<ObjectDefinition> ReskinSchema()
{
	Schema<ObjectDefinition> schema;
	AddCommon(schema);
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
	const auto objectSchema = detail::ObjectSchema();
	const auto reskinSchema = detail::ReskinSchema();
	const auto copyFrom = [](const ObjectDefinition &source) {
		ObjectDefinition copy = source;
		for (ModuleEntry &module : copy.modules)
			module.copied = true;
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
}
