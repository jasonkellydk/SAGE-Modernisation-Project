export module games.generalszh.content.objects.object_definition;
import std;

export import engine.config.binding.schema;
export import games.generalszh.content.objects.kind_of;
export import Engine.Core.Math.FixedVector;

export namespace generalszh::content
{
enum class ModuleSlot : std::uint8_t
{
	Behavior,
	Body,
	Draw,
	ClientUpdate
};

// One module line ("Draw = W3DModelDraw ModuleTag_01 ... End"). The block
// stays in its loaded document; module ports bind it into typed definitions.
struct ModuleEntry
{
	ModuleSlot slot{ModuleSlot::Behavior};
	std::string type;
	std::string tag;
	const engine::config::Node *block{nullptr};
	bool copied{false};      // came from DefaultThingTemplate or a reskin source
	bool inheritable{false}; // survives replacement by the inheriting object
	bool overrideableByLikeKind{false};
};

enum class GeometryShape : std::uint8_t
{
	Sphere,
	Cylinder,
	Box
};

struct Geometry
{
	GeometryShape shape{GeometryShape::Sphere};
	Engine::Math::Fixed majorRadius;
	Engine::Math::Fixed minorRadius;
	Engine::Math::Fixed height;
	bool small{false};
};

// GeometryInfo::m_boundingSphereRadius: a sphere's radius, a cylinder's larger of radius and half height,
// a box's corner in 3D.
inline Engine::Math::Fixed BoundingSphereRadius(const Geometry &geometry) noexcept
{
	using Engine::Math::Fixed;
	const Fixed half = geometry.height / Fixed::FromInt(2);
	switch (geometry.shape)
	{
	case GeometryShape::Cylinder: return half > geometry.majorRadius ? half : geometry.majorRadius;
	case GeometryShape::Box: return Engine::Math::Length(Engine::Math::FixedVector3{geometry.majorRadius, geometry.minorRadius, half});
	case GeometryShape::Sphere: break;
	}
	return geometry.majorRadius;
}

// GeometryInfo::m_boundingCircleRadius: a box's corner (sqrt(major^2 + minor^2)), else its major radius.
inline Engine::Math::Fixed BoundingCircleRadius(const Geometry &geometry) noexcept
{
	return geometry.shape == GeometryShape::Box ? Engine::Math::Length(Engine::Math::FixedVector2{geometry.majorRadius, geometry.minorRadius})
												: geometry.majorRadius;
}

// A placeable thing ("Object" / "ObjectReskin" block): plain data only.
struct ObjectDefinition
{
	std::string name;
	std::string reskinnedFrom;
	std::string side;
	std::string displayName;
	std::string buttonImage;    // its cameo on command and queue buttons (ButtonImage, a mapped image)
	std::string selectPortrait; // its portrait while selected (SelectPortrait)
	// The upgrades shown beside its portrait (UpgradeCameo1..5: ThingTemplate::getUpgradeCameoName), by upgrade name.
	std::array<std::string, 5> upgradeCameos;
	std::string editorSorting;
	std::string commandSet;
	// RadarPriority (INVALID when unset: the object decides; NOT_ON_RADAR, STRUCTURE, UNIT, LOCAL_UNIT_ONLY).
	std::string radarPriority;
	KindOfMask kinds{};
	Geometry geometry;
	Engine::Math::Fixed scale{Engine::Math::Fixed::One()};
	Engine::Math::Fixed instanceScaleFuzziness;
	// FenceWidth: a fence's span (GameLogic::startNewGame: a CLEARED_BY_BUILD map object without one is fluff).
	Engine::Math::Fixed fenceWidth;
	Engine::Math::Fixed visionRange;
	// ShroudClearingRange as authored (ThingTemplate's -1: not given; see ClearingRange).
	Engine::Math::Fixed shroudClearingRange{Engine::Math::Fixed::FromInt(-1)};
	// Object::Object: its shroud clearing range, its vision range when none was given.
	Engine::Math::Fixed ClearingRange() const noexcept { return shroudClearingRange == Engine::Math::Fixed::FromInt(-1) ? visionRange : shroudClearingRange; }
	Engine::Math::Fixed shroudRevealToAllRange; // ShroudRevealToAllRange: a reveal to its enemies and neutrals
	std::int32_t buildCost{0};
	bool buildFacility{false}; // isBuildFacility: another object's prerequisite, or a command centre (hasAnyBuildFacility)
	std::int32_t refundValue{0};
	// Buildable: Yes, Ignore_Prerequisites, No, Only_By_AI (BuildableStatus).
	enum class Buildable : std::uint8_t
	{
		Yes,
		IgnorePrerequisites,
		No,
		OnlyByAI,
	};
	Buildable buildable{Buildable::Yes};
	// MaxSimultaneousOfType (0: no limit; DeterminedBySuperweaponRestriction: the game's superweapon limit) and the key
	// counting others with it alike (MaxSimultaneousLinkKey).
	std::uint32_t maxSimultaneousOfType{0};
	bool maxSimultaneousBySuperweaponRestriction{false};
	std::string maxSimultaneousLinkKey;
	// Shadow (TheShadowNames bits: SHADOW_DECAL 1, SHADOW_VOLUME 2, SHADOW_PROJECTION 4, SHADOW_DYNAMIC_PROJECTION 8,
	// SHADOW_DIRECTIONAL_PROJECTION 16, SHADOW_ALPHA_DECAL 32, SHADOW_ADDITIVE_DECAL 64).
	std::uint8_t shadow{0}; // RefundValue: what selling it gives back (0: SellPercentage of its cost)
	Engine::Math::Fixed placementViewAngleDegrees; // PlacementViewAngle: how it faces when placed
	std::int32_t energyProduction{0}; // positive produces power, negative consumes it
	std::int32_t energyBonus{0};      // EnergyBonus: what control rods / overcharge add to its production
	// What must be owned to build it (Prerequisites): every group, one of each
	// group's objects ("Object = A B" is one group); and sciences, all of them.
	std::vector<std::vector<std::string>> prerequisiteObjects;
	std::vector<std::string> prerequisiteSciences;
	Engine::Math::Fixed buildTimeSeconds;
	std::int32_t transportSlots{0};
	// What it can crush and what crushes it (0: crushes nothing; 255: nothing crushes it).
	std::int32_t crusherLevel{0};
	std::int32_t crushableLevel{255};
	std::int32_t threat{0};
	// Veterancy (ExperienceValue / ExperienceRequired per level, IsTrainable).
	std::array<std::int32_t, 4> experienceValue{};
	std::array<std::int32_t, 4> experienceRequired{};
	// SkillPointValue per level (the general's points a kill of it is worth); -999 (USE_EXP_VALUE_FOR_SKILL_VALUE): its
	// ExperienceValue.
	std::array<std::int32_t, 4> skillPointValue{-999, -999, -999, -999};

	std::int32_t SkillPointValue(std::size_t level) const noexcept
	{
		return skillPointValue[level] == -999 ? experienceValue[level] : skillPointValue[level];
	}
	bool trainable{false};
	bool isBridge{false}; // IsBridge: a landmark bridge (ThingTemplate::isBridge)
	Engine::Math::Fixed structureRubbleHeight;
	std::vector<const engine::config::Node *> armorSets;
	std::vector<const engine::config::Node *> weaponSets;
	std::vector<const engine::config::Node *> locomotorSets;
	// Creating this object creates one of these instead, chosen at random.
	std::vector<std::string> buildVariations;
	std::vector<ModuleEntry> modules;
	// Sound events by role: "VoiceSelect", "SoundMoveLoop", "SoundAmbient", �
	// and the UnitSpecificSounds entries (e.g. "TruckLandingSound").
	std::map<std::string, std::string, std::less<>> sounds;
	// UnitSpecificFX: FX lists by role (e.g. "CombatDropKillFX"), ThingTemplate::getPerUnitFX.
	std::map<std::string, std::string, std::less<>> unitFx;

	std::string_view UnitFx(std::string_view role) const noexcept
	{
		const auto found = unitFx.find(role);
		return found != unitFx.end() ? std::string_view(found->second) : std::string_view{};
	}

	std::string_view Sound(std::string_view role) const noexcept
	{
		const auto found = sounds.find(role);
		return found != sounds.end() ? std::string_view(found->second) : std::string_view{};
	}
	std::vector<engine::config::SourceLocation> definedAt;

	// Is("STRUCTURE"): the literal's bit is a compile-time constant; a name known only at run time is looked up.
	bool Is(KindOfName kind) const noexcept { return HasKindOf(kinds, kind.bit); }
	template<typename Name>
		requires(!std::is_array_v<Name> && std::convertible_to<const Name &, std::string_view>)
	bool Is(const Name &kind) const noexcept
	{
		return HasKindOf(kinds, KindOfBit(std::string_view(kind)));
	}

	const ModuleEntry *FindModule(ModuleSlot slot) const noexcept
	{
		for (const ModuleEntry &module : modules)
			if (module.slot == slot)
				return &module;
		return nullptr;
	}
};
}
