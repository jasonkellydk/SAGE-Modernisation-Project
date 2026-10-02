export module games.generalszh.content.combat.combat_catalog;
import std;

export import engine.config.binding.schema;
export import engine.gameplay.common.health.definitions.armor;
import games.generalszh.content.objects.object_status;
export import engine.gameplay.common.weapons.definitions.weapon;
export import games.generalszh.content.combat.weapon_bonus_content;
export import engine.gameplay.rts.combat.components.turret;
export import games.generalszh.content.objects.object_definition;
export import games.generalszh.content.global.radius_decal;
import games.generalszh.content.combat.loadout_content;

// Zero Hour combat content: damage type names, "Armor" and "Weapon" blocks
// (Data/INI/Armor.ini, Data/INI/Weapon.ini) bound onto the engine's armor
// and weapon definitions, and each object's combat setup (body, armor set,
// weapon set, AI acquisition and turret) read from its blocks.
export namespace generalszh::content
{
// Zero Hour's damage types, in the original's order (their index is the type).
inline constexpr std::array<std::string_view, 41> DamageTypeNames{"EXPLOSION", "CRUSH", "ARMOR_PIERCING", "SMALL_ARMS", "GATTLING",
	"RADIATION", "FLAME", "LASER", "SNIPER", "POISON", "HEALING", "UNRESISTABLE", "WATER", "DEPLOY", "SURRENDER", "HACK", "KILL_PILOT",
	"PENALTY", "FALLING", "MELEE", "DISARM", "HAZARD_CLEANUP", "PARTICLE_BEAM", "TOPPLING", "INFANTRY_MISSILE", "AURORA_BOMB", "LAND_MINE",
	"JET_MISSILES", "STEALTHJET_MISSILES", "MOLOTOV_COCKTAIL", "COMANCHE_VULCAN", "SUBDUAL_MISSILE", "SUBDUAL_VEHICLE", "SUBDUAL_BUILDING",
	"SUBDUAL_UNRESISTABLE", "MICROWAVE", "KILL_GARRISONED", "STATUS"};

// Zero Hour's death types, in the original's order (their index is the type;
// weapons say which one they cause, die behaviours which ones they handle).
inline constexpr std::array<std::string_view, 21> DeathTypeNames{"NORMAL", "NONE", "CRUSHED", "BURNED", "EXPLODED", "POISONED", "TOPPLED",
	"FLOODED", "SUICIDED", "LASERED", "DETONATED", "SPLATTED", "POISONED_BETA", "EXTRA_2", "EXTRA_3", "EXTRA_4", "EXTRA_5", "EXTRA_6",
	"EXTRA_7", "EXTRA_8", "POISONED_GAMMA"};

// A weapon as authored: the simulation part plus what the client shows.
struct WeaponContent
{
	engine::gameplay::WeaponDefinition simulation;
	std::string projectileObject;
	// By its firer's veterancy level (REGULAR, VETERAN, ELITE, HEROIC): FireFX and the rest set every level, their
	// Veterancy* lines one level each, in the order given (empty or None: none).
	std::array<std::string, 4> fireFXs;
	std::array<std::string, 4> detonationFXs;
	std::array<std::string, 4> fireOCLs;       // created where it fires
	std::array<std::string, 4> detonationOCLs; // created where it lands (napalm fire fields, ...)
	std::string fireSound;
	// PlayFXWhenStealthed: its fire FX shows even when its firer cannot be seen (stealthed from the one watching).
	bool playFXWhenStealthed{false};
	// ShowsAmmoPips: the pips of its clip show under a selected firer's health bar (Drawable::drawAmmo).
	bool showsAmmoPips{false};
	// WeaponRecoil (INI::parseAngleReal, degrees): how hard firing rocks its firer's body back from the shot.
	Engine::Math::Fixed weaponRecoil;
	std::array<std::string, 4> exhausts;       // ProjectileExhaust: the particle system its missile trails once lit

	const std::string &FireFX(std::uint8_t level) const noexcept { return fireFXs[std::min<std::size_t>(level, 3)]; }
	const std::string &DetonationFX(std::uint8_t level) const noexcept { return detonationFXs[std::min<std::size_t>(level, 3)]; }
	const std::string &FireOCL(std::uint8_t level) const noexcept { return fireOCLs[std::min<std::size_t>(level, 3)]; }
	const std::string &DetonationOCL(std::uint8_t level) const noexcept { return detonationOCLs[std::min<std::size_t>(level, 3)]; }
	const std::string &Exhaust(std::uint8_t level) const noexcept { return exhausts[std::min<std::size_t>(level, 3)]; }
	std::string laser;         // LaserName: the beam object drawn from its LaserBoneName to what it hits
	std::string laserBone;
	std::string projectileStream; // ProjectileStreamName: the stream object drawn through its projectiles in flight
	std::string historicBonusWeapon; // HistoricBonusWeapon: fired where enough of its hits land together (none: empty)
	// Its own WeaponBonus lines (WeaponTemplate::m_extraBonus), when it has any.
	std::optional<engine::gameplay::WeaponBonusSet> extraBonus;
	// Its ScatterTarget lines (WeaponTemplate::m_scatterTargets), unscaled, in order.
	std::vector<Engine::Math::FixedVector2> scatterTargets;
};

// How an object fights, read from its modules and sets.
struct ObjectCombat
{
	std::optional<Engine::Math::Fixed> maxHealth;
	Engine::Math::Fixed initialHealth;
	// UndeadBody: its SecondLifeMaxHealth (none: not an undead body; the original's default 1).
	std::optional<Engine::Math::Fixed> secondLifeMaxHealth;
	// Its body keeps a last point: HighlanderBody (all but UNRESISTABLE damage), ImmortalBody (everything).
	enum class BodyFloor : std::uint8_t
	{
		None,
		Highlander,
		Immortal,
	};
	BodyFloor bodyFloor{BodyFloor::None};
	bool inactiveBody{false}; // InactiveBody: never hurt, effectively dead from the start
	// ActiveBody's SubdualDamageCap (above 0: it can be subdued), SubdualDamageHealAmount and SubdualDamageHealRate (ticks).
	Engine::Math::Fixed subdualCap;
	Engine::Math::Fixed subdualHealAmount;
	std::uint64_t subdualHealTicks{0};
	std::string armor;         // the armor of the set without conditions
	std::string primaryWeapon; // the PRIMARY weapon of the set without conditions
	// Its PRIMARY, SECONDARY and TERTIARY weapons (empty: none).
	std::array<std::string, 3> slotWeapons;
	// That set's AutoChooseSources and PreferredAgainst.
	engine::gameplay::SlotRules slotRules;
	// Which slots each turret aims (ControlledWeaponSlots, a bit per slot).
	std::uint8_t turretSlots{0};
	std::uint8_t altTurretSlots{0};
	// Its PointDefenseLaserUpdate: the laser's weapon, the kinds it shoots first and next (target class bits),
	// how often it scans (ticks) and how far.
	std::string pointDefenseWeapon;
	std::uint32_t pointDefensePrimary{0};
	std::uint32_t pointDefenseSecondary{0};
	std::uint32_t pointDefenseScanTicks{0};
	Engine::Math::Fixed pointDefenseRange;
	bool hasAI{false};
	bool autoAcquire{false};
	bool acquireStealthed{false}; // AutoAcquireEnemiesWhenIdle Stealthed
	bool acquireNotWhileAttacking{false}; // AutoAcquireEnemiesWhenIdle NotWhileAttacking
	bool attackBuildings{false};
	bool turret{false};
	// Its initial rider's model condition (RiderChangeContain: "RIDER2"), empty without one.
	std::string riderCondition;
	std::string riderWeaponCondition; // and the weapon set condition it arms ("WEAPON_RIDER2")
	std::string riderStatus;          // and the status it gives its vehicle ("STATUS_RIDER2")
	// Its AI's Turret block (TurretAI), when it has one.
	engine::gameplay::TurretDefinition turretDefinition;
	// Its AltTurret block, and whether both turrets aim together (TurretsLinked).
	bool altTurret{false};
	engine::gameplay::TurretDefinition altTurretDefinition;
	bool turretsLinked{false};
	std::uint64_t scanInterval{60}; // MoodAttackCheckRate: 2 s unless authored
};

// AssistedTargetingUpdate: the slot it helps with (AssistingWeaponSlot), for how many shots (AssistingClipSize), and the
// data streams drawn from whoever asked to it and from it to the target (LaserFromAssisted, LaserToTarget: laser objects).
struct AssistedTargetingContent
{
	std::uint8_t slot{1};
	std::uint32_t clip{0};
	std::string laserFromAssisted;
	std::string laserToTarget;
};

inline std::optional<AssistedTargetingContent> ReadAssistedTargeting(const ObjectDefinition &object)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "AssistedTargetingUpdate")
			continue;
		AssistedTargetingContent assist;
		if (const auto *slot = module.block->Find("AssistingWeaponSlot"))
			assist.slot = slot->Value() == "PRIMARY" ? 0u : slot->Value() == "TERTIARY" ? 2u : 1u;
		if (const auto *clip = module.block->Find("AssistingClipSize"))
			assist.clip = static_cast<std::uint32_t>(std::max<std::int64_t>(0, engine::config::values::ParseInt(clip->Value()).value_or(0)));
		if (const auto *laser = module.block->Find("LaserFromAssisted"))
			assist.laserFromAssisted = std::string(laser->Value());
		if (const auto *laser = module.block->Find("LaserToTarget"))
			assist.laserToTarget = std::string(laser->Value());
		return assist;
	}
	return std::nullopt;
}

// FireWeaponCollide: the weapon fired at what runs into it (CollideWeapon), whether it needs to burn (RequiredStatus
// AFLAME, the only status shipped data asks for) and whether it fires once only (FireOnce).
struct CollideWeaponContent
{
	std::string weapon;
	bool requiresAflame{false};
	bool fireOnce{false};
};

inline std::optional<CollideWeaponContent> ReadCollideWeapon(const ObjectDefinition &object)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "FireWeaponCollide")
			continue;
		CollideWeaponContent collide;
		if (const auto *weapon = module.block->Find("CollideWeapon"))
			collide.weapon = std::string(weapon->Value());
		if (const auto *required = module.block->Find("RequiredStatus"))
			for (const std::string_view status : required->values)
				collide.requiresAflame = collide.requiresAflame || status == "AFLAME";
		if (const auto *once = module.block->Find("FireOnce"))
			collide.fireOnce = engine::config::values::ParseBool(once->Value()).value_or(false);
		if (collide.weapon.empty())
			return std::nullopt;
		return collide;
	}
	return std::nullopt;
}

std::optional<std::uint32_t> DamageTypeIndex(std::string_view name)
{
	for (std::uint32_t index = 0; index < DamageTypeNames.size(); ++index)
	{
		const std::string_view known = DamageTypeNames[index];
		if (known.size() == name.size() && std::equal(known.begin(), known.end(), name.begin(), [](char a, char b) {
				return std::toupper(static_cast<unsigned char>(a)) == std::toupper(static_cast<unsigned char>(b));
			}))
			return index;
	}
	return std::nullopt;
}

// DamageTypeFlags as the original parses them (INI::parseDamageTypeFlags): from ALL each time the field is read, then
// ALL, NONE, +TYPE and -TYPE in turn.
std::uint64_t ParseDamageTypeFlags(const engine::config::Node &node)
{
	std::uint64_t flags = ~std::uint64_t{0};
	const auto same = [](std::string_view a, std::string_view b) {
		return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
			return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
		});
	};
	for (const std::string_view token : node.values)
	{
		if (same(token, "ALL"))
			flags = ~std::uint64_t{0};
		else if (same(token, "NONE"))
			flags = 0;
		else if (!token.empty() && (token[0] == '+' || token[0] == '-'))
			if (const auto type = DamageTypeIndex(token.substr(1)); type && *type < 64)
			{
				if (token[0] == '+')
					flags |= std::uint64_t{1} << *type;
				else
					flags &= ~(std::uint64_t{1} << *type);
			}
	}
	return flags;
}

// DeployStyleAIUpdate: UnpackTime and PackTime (ms up to whole ticks), TurretsFunctionOnlyWhenDeployed,
// TurretsMustCenterBeforePacking, ManualDeployAnimations.
struct DeployStyleContent
{
	std::uint64_t unpackTicks{0};
	std::uint64_t packTicks{0};
	bool turretsOnlyWhenDeployed{false};
	bool turretsMustCenter{false};
	bool manualAnimations{false};
};

std::optional<DeployStyleContent> ReadDeployStyle(const ObjectDefinition &object, std::uint64_t ticksPerSecond)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "DeployStyleAIUpdate")
			continue;
		DeployStyleContent deploy;
		const auto ticks = [&](std::string_view key) -> std::uint64_t {
			const engine::config::Node *node = module.block->Find(key);
			const std::int64_t ms = node != nullptr ? std::max<std::int64_t>(0, engine::config::values::ParseInt(node->Value()).value_or(0)) : 0;
			return static_cast<std::uint64_t>((ms * static_cast<std::int64_t>(ticksPerSecond) + 999) / 1000);
		};
		const auto yes = [&](std::string_view key) {
			const engine::config::Node *node = module.block->Find(key);
			return node != nullptr && engine::config::values::ParseBool(node->Value()).value_or(false);
		};
		deploy.unpackTicks = ticks("UnpackTime");
		deploy.packTicks = ticks("PackTime");
		deploy.turretsOnlyWhenDeployed = yes("TurretsFunctionOnlyWhenDeployed");
		deploy.turretsMustCenter = yes("TurretsMustCenterBeforePacking");
		deploy.manualAnimations = yes("ManualDeployAnimations");
		return deploy;
	}
	return std::nullopt;
}

// NeutronMissileUpdate: DistanceToTravelBeforeTurning, MaxTurnRate (degrees a second), ForwardDamping, RelativeSpeed,
// TargetFromDirectlyAbove, SpecialSpeedTime (ms up to whole ticks), SpecialSpeedHeight, SpecialAccelFactor, LaunchFX,
// IgnitionFX.
struct NeutronMissileContent
{
	engine::gameplay::NeutronMissileDefinition flight;
	std::string launchFX;
	std::string ignitionFX;
	// DeliveryDecal and DeliveryDecalRadius: laid on its target once fired at it.
	RadiusDecalLook deliveryDecal;
	Engine::Math::Fixed deliveryDecalRadius;
	// SpecialJitterDistance: how far its drawing shakes as its climb begins (presentation).
	Engine::Math::Fixed specialJitter;
};

std::optional<NeutronMissileContent> ReadNeutronMissile(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "NeutronMissileUpdate")
			continue;
		NeutronMissileContent out;
		engine::gameplay::NeutronMissileDefinition &d = out.flight;
		const auto real = [&](std::string_view key, Engine::Math::Fixed fallback) {
			const engine::config::Node *node = module.block->Find(key);
			return node != nullptr ? engine::config::values::ParseFixed(node->Value()).value_or(fallback) : fallback;
		};
		d.initialDistance = real("DistanceToTravelBeforeTurning", Engine::Math::Fixed{});
		// parseAngularVelocityReal: degrees a second -> radians a frame (999 by default: any turn at once).
		if (const engine::config::Node *rate = module.block->Find("MaxTurnRate"))
		{
			const Engine::Math::Fixed perTick = step.PerTick(engine::config::values::ParseFixed(rate->Value()).value_or(Engine::Math::Fixed{}));
			d.maxTurnRate = perTick >= Engine::Math::Fixed::FromInt(180) ? Engine::Math::TurnFromDegrees(Engine::Math::Fixed::FromInt(180)) : Engine::Math::TurnFromDegrees(perTick);
		}
		d.forwardDamping = real("ForwardDamping", Engine::Math::Fixed{});
		d.relativeSpeed = real("RelativeSpeed", Engine::Math::Fixed::One());
		d.targetFromAbove = real("TargetFromDirectlyAbove", Engine::Math::Fixed{});
		if (const engine::config::Node *time = module.block->Find("SpecialSpeedTime"))
		{
			const std::int64_t ms = std::max<std::int64_t>(0, engine::config::values::ParseInt(time->Value()).value_or(0));
			d.specialSpeedTicks = static_cast<std::uint64_t>((ms * static_cast<std::int64_t>(step.TicksPerSecond()) + 999) / 1000);
		}
		d.specialSpeedHeight = real("SpecialSpeedHeight", Engine::Math::Fixed{});
		d.specialAccelFactor = real("SpecialAccelFactor", Engine::Math::Fixed::One());
		d.boundingRadius = BoundingSphereRadius(object.geometry);
		if (const engine::config::Node *fx = module.block->Find("LaunchFX"); fx != nullptr && !fx->values.empty())
			out.launchFX = std::string(fx->Value());
		if (const engine::config::Node *fx = module.block->Find("IgnitionFX"); fx != nullptr && !fx->values.empty())
			out.ignitionFX = std::string(fx->Value());
		if (const engine::config::Node *decal = module.block->Find("DeliveryDecal"))
			out.deliveryDecal = ReadRadiusDecal(*decal, step.TicksPerSecond());
		out.deliveryDecalRadius = real("DeliveryDecalRadius", Engine::Math::Fixed{});
		out.specialJitter = real("SpecialJitterDistance", Engine::Math::Fixed{});
		return out;
	}
	return std::nullopt;
}

// HiveStructureBody: the damage types it passes to its spawn nearest the shooter while it has any, and those it
// swallows when it has none.
struct HiveBodyContent
{
	std::uint64_t propagate{0};
	std::uint64_t swallow{0};
};

std::optional<HiveBodyContent> ReadHiveBody(const ObjectDefinition &object)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Body || module.block == nullptr || module.type != "HiveStructureBody")
			continue;
		HiveBodyContent hive;
		if (const engine::config::Node *types = module.block->Find("PropagateDamageTypesToSlavesWhenExisting"))
			hive.propagate = ParseDamageTypeFlags(*types);
		if (const engine::config::Node *types = module.block->Find("SwallowDamageTypesIfSlavesNotExisting"))
			hive.swallow = ParseDamageTypeFlags(*types);
		return hive;
	}
	return std::nullopt;
}

std::optional<std::uint32_t> DeathTypeIndex(std::string_view name)
{
	for (std::uint32_t index = 0; index < DeathTypeNames.size(); ++index)
	{
		const std::string_view known = DeathTypeNames[index];
		if (known.size() == name.size() && std::equal(known.begin(), known.end(), name.begin(), [](char a, char b) {
				return std::toupper(static_cast<unsigned char>(a)) == std::toupper(static_cast<unsigned char>(b));
			}))
			return index;
	}
	return std::nullopt;
}

namespace detail
{
using engine::config::BindContext;
using engine::config::Node;

bool SameText(std::string_view a, std::string_view b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
	});
}

std::optional<Engine::Math::Fixed> Percent(std::string_view token)
{
	if (!token.empty() && token.back() == '%')
		token.remove_suffix(1);
	const auto value = engine::config::values::ParseFixed(token);
	return value ? std::optional(*value / Engine::Math::Fixed::FromInt(100)) : std::nullopt;
}

// Milliseconds -> ticks, rounded up (the original's duration fields).
std::uint64_t Ticks(std::int64_t milliseconds, const engine::time::FixedStep &step)
{
	if (milliseconds <= 0)
		return 0;
	return static_cast<std::uint64_t>((milliseconds * static_cast<std::int64_t>(step.TicksPerSecond()) + 999) / 1000);
}

// Degrees -> signed turn units, -180 and 180 (and beyond) the ends of the range.
std::int32_t SignedTurn(Engine::Math::Fixed degrees)
{
	if (degrees <= Engine::Math::Fixed::FromInt(-180))
		return std::numeric_limits<std::int32_t>::min();
	if (degrees >= Engine::Math::Fixed::FromInt(180))
		return std::numeric_limits<std::int32_t>::max();
	return static_cast<std::int32_t>(Engine::Math::TurnFromDegrees(degrees).units);
}

using WeaponHandler = std::function<void(const Node &, WeaponContent &, BindContext &)>;

// parseAllVetLevelsFXList / parseAllVetLevelsAsciiString / parseAllVetLevelsPSys: the name at every veterancy level;
// parsePerVetLevel*: "<LEVEL> <name>" at that level (TheVeterancyNames). None: none.
std::string NameOrNone(std::string_view name) { return SameText(name, "None") ? std::string{} : std::string(name); }

WeaponHandler AllLevels(std::array<std::string, 4> WeaponContent::*member)
{
	return [member](const Node &node, WeaponContent &out, BindContext &) { (out.*member).fill(NameOrNone(node.Value())); };
}

WeaponHandler OneLevel(std::array<std::string, 4> WeaponContent::*member)
{
	return [member](const Node &node, WeaponContent &out, BindContext &) {
		static constexpr std::array<std::string_view, 4> Levels{"REGULAR", "VETERAN", "ELITE", "HEROIC"};
		if (node.values.size() < 2)
			return;
		for (std::size_t level = 0; level < Levels.size(); ++level)
			if (SameText(node.Value(0), Levels[level]))
				(out.*member)[level] = NameOrNone(node.Value(1));
	};
}

WeaponHandler FixedField(Engine::Math::Fixed engine::gameplay::WeaponDefinition::*member)
{
	return [member](const Node &node, WeaponContent &out, BindContext &context) {
		if (const auto value = engine::config::ReadFixed(node, context))
			out.simulation.*member = *value;
	};
}

WeaponHandler AntiBit(std::uint32_t bit)
{
	return [bit](const Node &node, WeaponContent &out, BindContext &context) {
		if (const auto value = engine::config::ReadBool(node, context))
			out.simulation.anti = *value ? (out.simulation.anti | bit) : (out.simulation.anti & ~bit);
	};
}
}

engine::config::DefinitionTable<engine::gameplay::ArmorDefinition> BuildArmorCatalog(const engine::config::Document &document,
	engine::config::BindContext &context)
{
	using engine::gameplay::ArmorDefinition;
	engine::config::Schema<ArmorDefinition> schema;
	schema.On("Armor", [](const engine::config::Node &node, ArmorDefinition &out, engine::config::BindContext &bind) {
		// Unresistable damage ignores armor, as the original's adjustDamage.
		out.bypass = (std::uint64_t{1} << *DamageTypeIndex("UNRESISTABLE")) | (std::uint64_t{1} << *DamageTypeIndex("SUBDUAL_UNRESISTABLE"));
		const auto percent = detail::Percent(node.Value(1));
		if (!percent)
		{
			bind.diagnostics.Warning(node.location, "armor coefficient needs a percentage");
			return;
		}
		if (detail::SameText(node.Value(0), "DEFAULT"))
		{
			out.coefficient.fill(*percent);
			return;
		}
		if (const auto type = DamageTypeIndex(node.Value(0)))
			out.coefficient[*type] = *percent;
		else
			bind.diagnostics.Warning(node.location, "unknown damage type '" + std::string(node.Value(0)) + "'");
	});
	schema.Ignore("DamageFX");
	engine::config::DefinitionTable<ArmorDefinition> armors;
	engine::config::BindBlocks(document, "Armor", schema, armors, context, engine::config::Redefinition::Replace);
	return armors;
}

engine::config::DefinitionTable<WeaponContent> BuildWeaponCatalog(const engine::config::Document &document, engine::config::BindContext &context)
{
	using engine::config::BindContext;
	using engine::config::Node;
	using engine::gameplay::WeaponDefinition;
	namespace anti = engine::gameplay::weapon_anti;
	namespace affects = engine::gameplay::weapon_affects;
	engine::config::Schema<WeaponContent> schema;
	schema.On("PrimaryDamage", detail::FixedField(&WeaponDefinition::primaryDamage))
		.On("PrimaryDamageRadius", detail::FixedField(&WeaponDefinition::primaryRadius))
		.On("ShockWaveAmount", detail::FixedField(&WeaponDefinition::shockWaveAmount))
		.On("ShockWaveRadius", detail::FixedField(&WeaponDefinition::shockWaveRadius))
		.On("ShockWaveTaperOff", detail::FixedField(&WeaponDefinition::shockWaveTaperOff))
		.On("SecondaryDamage", detail::FixedField(&WeaponDefinition::secondaryDamage))
		.On("SecondaryDamageRadius", detail::FixedField(&WeaponDefinition::secondaryRadius))
		.On("AttackRange", detail::FixedField(&WeaponDefinition::attackRange))
		.On("MinimumAttackRange", detail::FixedField(&WeaponDefinition::minimumRange))
		.On("RequestAssistRange", detail::FixedField(&WeaponDefinition::requestAssistRange))
		.On("ScatterRadius", detail::FixedField(&WeaponDefinition::scatterRadius))
		.On("ScatterRadiusVsInfantry", detail::FixedField(&WeaponDefinition::infantryScatter))
		// INI::parseAngleReal (degrees); the limits kept within a half turn either way.
		.On("MinTargetPitch", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto value = engine::config::ReadFixed(node, bind))
				out.simulation.minTargetPitch = detail::SignedTurn(*value);
		})
		.On("MaxTargetPitch", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto value = engine::config::ReadFixed(node, bind))
				out.simulation.maxTargetPitch = detail::SignedTurn(*value);
		})
		.On("AllowAttackGarrisonedBldgs", [](const Node &node, WeaponContent &out, BindContext &) {
			out.simulation.allowAttackGarrisoned = !node.values.empty() && engine::config::values::ParseBool(node.Value()).value_or(false);
		})
		.On("MinWeaponSpeed", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto value = engine::config::ReadPerSecond(node, bind))
				out.simulation.minWeaponSpeed = *value;
		})
		.On("ScaleWeaponSpeed", [](const Node &node, WeaponContent &out, BindContext &) {
			out.simulation.scaleWeaponSpeed = !node.values.empty() && engine::config::values::ParseBool(node.Value()).value_or(false);
		})
		.On("ShotsPerBarrel", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto value = engine::config::ReadInt(node, bind))
				out.simulation.shotsPerBarrel = static_cast<std::uint32_t>(std::clamp<std::int64_t>(*value, 1, 255));
		})
		.On("LeechRangeWeapon", [](const Node &node, WeaponContent &out, BindContext &) {
			out.simulation.leechRange = !node.values.empty() && engine::config::values::ParseBool(node.Value()).value_or(false);
		})
		.On("ShowsAmmoPips", [](const Node &node, WeaponContent &out, BindContext &) {
			out.showsAmmoPips = !node.values.empty() && engine::config::values::ParseBool(node.Value()).value_or(false);
		})
		.On("WeaponRecoil", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto value = engine::config::ReadFixed(node, bind))
				out.weaponRecoil = *value;
		})
		.On("PlayFXWhenStealthed", [](const Node &node, WeaponContent &out, BindContext &) {
			out.playFXWhenStealthed = !node.values.empty() && engine::config::values::ParseBool(node.Value()).value_or(false);
		})
		// INI::parseAngleReal (degrees): below PI, a cone.
		.On("RadiusDamageAngle", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto degrees = engine::config::ReadFixed(node, bind))
			{
				out.simulation.coned = *degrees < Engine::Math::Fixed::FromInt(180);
				out.simulation.coneCosine = Engine::Math::Cos(Engine::Math::TurnFromDegrees(*degrees));
			}
		})
		.On("SuspendFXDelay", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto value = engine::config::ReadFixed(node, bind))
				out.simulation.suspendFxTicks = detail::Ticks(value->Ceil(), bind.step);
		})
		.On("MissileCallsOnDie", [](const Node &node, WeaponContent &out, BindContext &) {
			out.simulation.missileCallsOnDie = !node.values.empty() && engine::config::values::ParseBool(node.Value()).value_or(false);
		})
		.On("ContinueAttackRange", detail::FixedField(&WeaponDefinition::continueAttackRange))
		.On("ScatterTargetScalar", detail::FixedField(&WeaponDefinition::scatterTargetScalar))
		// WeaponTemplate::parseScatterTarget: each line adds one (INI::parseCoord2D, "X:0.1 Y:-0.2"; a missing one is 0).
		.On("ScatterTarget", [](const Node &node, WeaponContent &out, BindContext &) {
			if (out.scatterTargets.size() >= engine::gameplay::ScatterTargetMax)
				return;
			Engine::Math::FixedVector2 target;
			for (std::size_t index = 0; index < node.values.size(); ++index)
			{
				std::string_view token = node.values[index];
				Engine::Math::Fixed *axis = token.starts_with("X:") || token.starts_with("x:") ? &target.x
					: token.starts_with("Y:") || token.starts_with("y:") ? &target.y : nullptr;
				if (axis == nullptr)
					continue;
				token.remove_prefix(2);
				if (token.empty() && index + 1 < node.values.size())
					token = node.values[++index];
				*axis = engine::config::values::ParseFixed(token).value_or(Engine::Math::Fixed{});
			}
			out.scatterTargets.push_back(target);
		})
		.On("WeaponSpeed", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto value = engine::config::ReadPerSecond(node, bind))
				out.simulation.speed = *value;
		})
		.On("AcceptableAimDelta", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto value = engine::config::ReadDegrees(node, bind))
				out.simulation.aimDelta = *value;
		})
		.On("DamageType", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto type = DamageTypeIndex(node.Value()))
				out.simulation.damageType = *type;
			else
				bind.diagnostics.Warning(node.location, "unknown damage type '" + std::string(node.Value()) + "'");
		})
		.On("DeathType", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto type = DeathTypeIndex(node.Value()))
				out.simulation.deathType = *type;
			else
				bind.diagnostics.Warning(node.location, "unknown death type '" + std::string(node.Value()) + "'");
		})
		.On("DelayBetweenShots", [](const Node &node, WeaponContent &out, BindContext &bind) {
			// "500" or "Min:500 Max:1000" (milliseconds).
			std::vector<std::string_view> tokens;
			std::string_view text = node.text;
			while (!text.empty())
			{
				const std::size_t skip = text.find_first_not_of(" \t:=");
				if (skip == std::string_view::npos)
					break;
				text.remove_prefix(skip);
				const std::size_t length = std::min(text.find_first_of(" \t:="), text.size());
				tokens.push_back(text.substr(0, length));
				text.remove_prefix(length);
			}
			std::int64_t low = 0, high = 0;
			if (!tokens.empty() && detail::SameText(tokens.front(), "Min"))
			{
				for (std::size_t index = 0; index + 1 < tokens.size(); index += 2)
				{
					const auto number = engine::config::values::ParseFixed(tokens[index + 1]);
					if (number && detail::SameText(tokens[index], "Min"))
						low = number->Ceil();
					else if (number && detail::SameText(tokens[index], "Max"))
						high = number->Ceil();
				}
				high = std::max(high, low);
			}
			else if (const auto number = engine::config::ReadFixed(node, bind))
				low = high = number->Ceil();
			out.simulation.delayMin = detail::Ticks(low, bind.step);
			out.simulation.delayMax = detail::Ticks(high, bind.step);
		})
		.On("ClipSize", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto value = engine::config::ReadInt(node, bind))
				out.simulation.clipSize = static_cast<std::uint32_t>(std::max<std::int64_t>(*value, 0));
		})
		.On("ContinuousFireOne", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto value = engine::config::ReadInt(node, bind))
				out.simulation.continuousFireOne = static_cast<std::uint32_t>(std::max<std::int64_t>(*value, 0));
		})
		.On("ContinuousFireTwo", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto value = engine::config::ReadInt(node, bind))
				out.simulation.continuousFireTwo = static_cast<std::uint32_t>(std::max<std::int64_t>(*value, 0));
		})
		.On("ContinuousFireCoast", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto value = engine::config::ReadDurationTicks(node, bind))
				out.simulation.continuousFireCoast = *value;
		})
		.On("DamageStatusType", [](const Node &node, WeaponContent &out, BindContext &) {
			if (const std::uint32_t bit = ObjectStatusBit(node.Value()); bit != NoStatus)
				out.simulation.damageStatusType = bit;
		})
		.On("AutoReloadWhenIdle", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto value = engine::config::ReadDurationTicks(node, bind))
				out.simulation.autoReloadIdleTicks = *value;
		})
		.On("FireSoundLoopTime", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto value = engine::config::ReadDurationTicks(node, bind))
				out.simulation.fireSoundLoopTicks = *value;
		})
		.On("ClipReloadTime", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto value = engine::config::ReadDurationTicks(node, bind))
				out.simulation.clipReload = *value;
		})
		.On("PreAttackDelay", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto value = engine::config::ReadDurationTicks(node, bind))
				out.simulation.preAttackDelay = *value;
		})
		.On("PreAttackType", [](const Node &node, WeaponContent &out, BindContext &) {
			using PreAttack = engine::gameplay::WeaponDefinition::PreAttack;
			const std::string_view value = node.Value();
			if (value == "PER_ATTACK")
				out.simulation.preAttackType = PreAttack::PerAttack;
			else if (value == "PER_CLIP")
				out.simulation.preAttackType = PreAttack::PerClip;
			else
				out.simulation.preAttackType = PreAttack::PerShot;
		})
		.On("DamageDealtAtSelfPosition", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto value = engine::config::ReadBool(node, bind))
				out.simulation.damageAtSelf = *value;
		})
		.On("RadiusDamageAffects", [](const Node &node, WeaponContent &out, BindContext &bind) {
			std::uint32_t mask = 0;
			for (const std::string_view token : node.values)
			{
				constexpr std::array<std::pair<std::string_view, std::uint32_t>, 7> names{{{"SELF", affects::Self}, {"ALLIES", affects::Allies},
					{"ENEMIES", affects::Enemies}, {"NEUTRALS", affects::Neutrals}, {"SUICIDE", affects::Suicide},
					{"NOT_SIMILAR", affects::NotSimilar}, {"NOT_AIRBORNE", affects::NotAirborne}}};
				bool known = false;
				for (const auto &[name, bit] : names)
					if (detail::SameText(token, name))
					{
						mask |= bit;
						known = true;
					}
				if (!known && !detail::SameText(token, "NONE"))
					bind.diagnostics.Warning(node.location, "unknown RadiusDamageAffects flag '" + std::string(token) + "'");
			}
			out.simulation.affects = mask;
		})
		.On("AntiAirborneVehicle", detail::AntiBit(anti::AirborneVehicle))
		.On("AntiGround", detail::AntiBit(anti::Ground))
		.On("AntiProjectile", detail::AntiBit(anti::Projectile))
		.On("AntiSmallMissile", detail::AntiBit(anti::SmallMissile))
		.On("AntiMine", detail::AntiBit(anti::Mine))
		.On("AntiParachute", detail::AntiBit(anti::Parachute))
		.On("AntiAirborneInfantry", detail::AntiBit(anti::AirborneInfantry))
		.On("AntiBallisticMissile", detail::AntiBit(anti::BallisticMissile))
		.On("ProjectileObject", [](const Node &node, WeaponContent &out, BindContext &) {
			out.projectileObject = std::string(node.Value());
			out.simulation.projectile = !out.projectileObject.empty() && !detail::SameText(out.projectileObject, "NONE");
		})
		.On("FireFX", detail::AllLevels(&WeaponContent::fireFXs))
		.On("ProjectileDetonationFX", detail::AllLevels(&WeaponContent::detonationFXs))
		.On("VeterancyFireFX", detail::OneLevel(&WeaponContent::fireFXs))
		.On("VeterancyProjectileDetonationFX", detail::OneLevel(&WeaponContent::detonationFXs))
		.On("VeterancyFireOCL", detail::OneLevel(&WeaponContent::fireOCLs))
		.On("VeterancyProjectileDetonationOCL", detail::OneLevel(&WeaponContent::detonationOCLs))
		.On("VeterancyProjectileExhaust", detail::OneLevel(&WeaponContent::exhausts))
		.On("AutoReloadsClip", [](const Node &node, WeaponContent &out, BindContext &) {
			out.simulation.reloadsAtBase = detail::SameText(node.Value(), "RETURN_TO_BASE");
			out.simulation.noReload = detail::SameText(node.Value(), "NO");
		})
		.On("FireOCL", detail::AllLevels(&WeaponContent::fireOCLs))
		.On("ProjectileDetonationOCL", detail::AllLevels(&WeaponContent::detonationOCLs))
		.On("FireSound", [](const Node &node, WeaponContent &out, BindContext &) { out.fireSound = std::string(node.Value()); })
		.On("ProjectileExhaust", detail::AllLevels(&WeaponContent::exhausts))
		// Weapon::isLaser: any LaserName at all (m_laserName.isNotEmpty()), whether or not its object exists.
		.On("LaserName", [](const Node &node, WeaponContent &out, BindContext &) {
			out.laser = std::string(node.Value());
			out.simulation.laser = !out.laser.empty();
		})
		.On("ProjectileCollidesWith", [](const Node &node, WeaponContent &out, BindContext &) {
			static constexpr std::pair<std::string_view, std::uint32_t> names[] = {{"ALLIES", engine::gameplay::weapon_collides::Allies},
				{"ENEMIES", engine::gameplay::weapon_collides::Enemies}, {"STRUCTURES", engine::gameplay::weapon_collides::Structures},
				{"SHRUBBERY", engine::gameplay::weapon_collides::Shrubbery}, {"PROJECTILES", engine::gameplay::weapon_collides::Projectiles},
				{"WALLS", engine::gameplay::weapon_collides::Walls}, {"SMALL_MISSILES", engine::gameplay::weapon_collides::SmallMissiles},
				{"BALLISTIC_MISSILES", engine::gameplay::weapon_collides::BallisticMissiles},
				{"CONTROLLED_STRUCTURES", engine::gameplay::weapon_collides::ControlledStructures}};
			out.simulation.collides = 0;
			for (const std::string_view value : node.values)
				for (const auto &[name, bit] : names)
					if (detail::SameText(value, name))
						out.simulation.collides |= bit;
		})
		.On("LaserBoneName", [](const Node &node, WeaponContent &out, BindContext &) { out.laserBone = std::string(node.Value()); })
		.On("ProjectileStreamName", [](const Node &node, WeaponContent &out, BindContext &) { out.projectileStream = std::string(node.Value()); })
		// WeaponTemplate's historic bonus: HistoricBonusTime (parseDurationUnsignedInt), Radius, Count and Weapon.
		.On("HistoricBonusTime", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto value = engine::config::ReadFixed(node, bind))
				out.simulation.historicBonusTicks = detail::Ticks(value->Ceil(), bind.step);
		})
		.On("HistoricBonusRadius", detail::FixedField(&WeaponDefinition::historicBonusRadius))
		.On("HistoricBonusCount", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto value = engine::config::ReadInt(node, bind))
				out.simulation.historicBonusCount = static_cast<std::uint32_t>(std::max<std::int64_t>(*value, 0));
		})
		.On("HistoricBonusWeapon", [](const Node &node, WeaponContent &out, BindContext &) {
			if (!node.Value().empty() && node.Value() != "None")
				out.historicBonusWeapon = std::string(node.Value());
		})
		.On("WeaponBonus", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (!out.extraBonus)
				out.extraBonus.emplace();
			ReadWeaponBonus(node, *out.extraBonus, bind);
		})
		.On("CapableOfFollowingWaypoints", [](const Node &node, WeaponContent &out, BindContext &bind) {
			if (const auto value = engine::config::ReadBool(node, bind))
				out.simulation.followsWaypoints = *value;
		});
	engine::config::DefinitionTable<WeaponContent> weapons;
	engine::config::BindBlocks(document, "Weapon", schema, weapons, context, engine::config::Redefinition::Replace);
	return weapons;
}

ObjectCombat ReadObjectCombat(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	using engine::config::Node;
	ObjectCombat combat;
	const auto fixed = [](const Node *node) -> std::optional<Engine::Math::Fixed> {
		return node != nullptr ? engine::config::values::ParseFixed(node->Value()) : std::nullopt;
	};
	// The object's own body and AI win over ones copied from its template.
	const ModuleEntry *body = nullptr;
	const ModuleEntry *ai = nullptr;
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr)
			continue;
		if (module.slot == ModuleSlot::Body && (body == nullptr || (body->copied && !module.copied)))
			body = &module;
		if (module.slot == ModuleSlot::Behavior && std::string_view(module.type).find("AIUpdate") != std::string_view::npos &&
			(ai == nullptr || (ai->copied && !module.copied)))
			ai = &module;
	}
	if (body != nullptr)
	{
		combat.maxHealth = fixed(body->block->Find("MaxHealth"));
		combat.initialHealth = fixed(body->block->Find("InitialHealth")).value_or(combat.maxHealth.value_or(Engine::Math::Fixed{}));
		combat.inactiveBody = body->type == "InactiveBody";
		combat.subdualCap = fixed(body->block->Find("SubdualDamageCap")).value_or(Engine::Math::Fixed{});
		combat.subdualHealAmount = fixed(body->block->Find("SubdualDamageHealAmount")).value_or(Engine::Math::Fixed{});
		if (const auto rate = fixed(body->block->Find("SubdualDamageHealRate")))
			combat.subdualHealTicks = detail::Ticks(rate->Ceil(), step);
		if (body->type == "UndeadBody")
			combat.secondLifeMaxHealth = fixed(body->block->Find("SecondLifeMaxHealth")).value_or(Engine::Math::Fixed::One());
		combat.bodyFloor = body->type == "HighlanderBody" ? ObjectCombat::BodyFloor::Highlander
			: body->type == "ImmortalBody" ? ObjectCombat::BodyFloor::Immortal : ObjectCombat::BodyFloor::None;
	}
	if (ai != nullptr)
	{
		combat.hasAI = true;
		if (const Node *acquire = ai->block->Find("AutoAcquireEnemiesWhenIdle"))
			for (const std::string_view flag : acquire->values)
			{
				combat.autoAcquire = combat.autoAcquire || detail::SameText(flag, "Yes");
				combat.acquireStealthed = combat.acquireStealthed || detail::SameText(flag, "Stealthed");
				combat.attackBuildings = combat.attackBuildings || detail::SameText(flag, "ATTACK_BUILDINGS");
				combat.acquireNotWhileAttacking = combat.acquireNotWhileAttacking || detail::SameText(flag, "NotWhileAttacking");
			}
		if (const auto rate = fixed(ai->block->Find("MoodAttackCheckRate")))
			combat.scanInterval = std::max<std::uint64_t>(1, detail::Ticks(rate->Ceil(), step));
		// Each turret block (TurretAI): Turret, and AltTurret with its own slot (linked: TurretsLinked).
		const auto slotsOf = [&](const Node *turret) {
			std::uint8_t bits = 0;
			if (const Node *slots = turret->Find("ControlledWeaponSlots"))
				for (const std::string_view slot : slots->values)
					bits |= detail::SameText(slot, "PRIMARY") ? 1u : detail::SameText(slot, "SECONDARY") ? 2u : detail::SameText(slot, "TERTIARY") ? 4u : 0u;
			return bits;
		};
		const auto readTurret = [&](const Node *turret, engine::gameplay::TurretDefinition &d) {
			const auto degrees = [&](std::string_view key, Engine::Math::TurnAngle &out) {
				if (const auto value = fixed(turret->Find(key)))
					out = Engine::Math::TurnFromDegrees(*value);
			};
			const auto perSecond = [&](std::string_view key, Engine::Math::TurnAngle &out) {
				if (const auto value = fixed(turret->Find(key)))
					out = Engine::Math::TurnFromDegrees(step.PerTick(*value));
			};
			perSecond("TurretTurnRate", d.turnRate);
			perSecond("TurretPitchRate", d.pitchRate);
			degrees("NaturalTurretAngle", d.naturalAngle);
			degrees("NaturalTurretPitch", d.naturalPitch);
			degrees("FirePitch", d.firePitch);
			degrees("MinPhysicalPitch", d.minPitch);
			degrees("GroundUnitPitch", d.groundUnitPitch);
			// TurretAIData::parseTurretSweep / parseTurretSweepSpeed: "<slot> <value>", each line for its slot.
			const auto perSlot = [&](std::string_view key, const auto &store) {
				for (const Node &line : turret->children)
					if (detail::SameText(line.key, key) && line.values.size() >= 2)
					{
						const std::string_view slot = line.values[0];
						const std::size_t index = detail::SameText(slot, "PRIMARY") ? 0 : detail::SameText(slot, "SECONDARY") ? 1 : detail::SameText(slot, "TERTIARY") ? 2 : 3;
						if (index < 3)
							if (const auto value = engine::config::values::ParseFixed(line.values[1]))
								store(index, *value);
					}
			};
			perSlot("TurretFireAngleSweep", [&](std::size_t slot, Engine::Math::Fixed value) { d.sweep[slot] = Engine::Math::TurnFromDegrees(value); });
			perSlot("TurretSweepSpeedModifier", [&](std::size_t slot, Engine::Math::Fixed value) { d.sweepSpeed[slot] = value; });
			if (const auto recenter = fixed(turret->Find("RecenterTime")))
				d.recenterTicks = detail::Ticks(recenter->Ceil(), step);
			// Scan bounds unwrapped (360 is a full turn, not none): turn units = degrees * 2^32 / 360.
			const auto scanUnits = [&](std::string_view key, std::int64_t &out) {
				if (const auto value = fixed(turret->Find(key)))
					out = value->Raw() * (std::int64_t{1} << 32) / (std::int64_t{360} << Engine::Math::Fixed::FractionBits);
			};
			scanUnits("MinIdleScanAngle", d.minScanUnits);
			scanUnits("MaxIdleScanAngle", d.maxScanUnits);
			if (const auto interval = fixed(turret->Find("MinIdleScanInterval")))
				d.minScanTicks = detail::Ticks(interval->Ceil(), step);
			if (const auto interval = fixed(turret->Find("MaxIdleScanInterval")))
				d.maxScanTicks = detail::Ticks(interval->Ceil(), step);
			if (const Node *pitch = turret->Find("AllowsPitch"))
				d.allowsPitch = detail::SameText(pitch->Value(), "Yes");
			if (const Node *turning = turret->Find("FiresWhileTurning"))
				d.firesWhileTurning = detail::SameText(turning->Value(), "Yes");
			if (const Node *off = turret->Find("InitiallyDisabled"))
				d.initiallyDisabled = detail::SameText(off->Value(), "Yes");
		};
		if (const Node *turret = ai->block->Find("Turret"))
		{
			combat.turret = true;
			readTurret(turret, combat.turretDefinition);
			combat.turretSlots = slotsOf(turret);
		}
		if (const Node *turret = ai->block->Find("AltTurret"))
		{
			combat.altTurret = true;
			readTurret(turret, combat.altTurretDefinition);
			combat.altTurretSlots = slotsOf(turret);
		}
		if (const Node *linked = ai->block->Find("TurretsLinked"))
			combat.turretsLinked = detail::SameText(linked->Value(), "Yes");
	}
	for (const ModuleEntry &module : object.modules)
		if (module.block != nullptr && module.type == "PointDefenseLaserUpdate")
		{
			const auto kinds = [&](std::string_view key) {
				std::uint32_t classes = 0;
				if (const Node *node = module.block->Find(key))
					for (const std::string_view kind : node->values)
					{
						const std::pair<std::string_view, std::uint32_t> names[] = {{"SMALL_MISSILE", engine::gameplay::target_class::SmallMissile},
							{"BALLISTIC_MISSILE", engine::gameplay::target_class::BallisticMissile}, {"PROJECTILE", engine::gameplay::target_class::Projectile},
							{"INFANTRY", engine::gameplay::target_class::Infantry}, {"VEHICLE", engine::gameplay::target_class::Vehicle},
							{"AIRCRAFT", engine::gameplay::target_class::Aircraft}, {"STRUCTURE", engine::gameplay::target_class::Structure},
							{"MINE", engine::gameplay::target_class::Mine}};
						for (const auto &[name, bit] : names)
							if (detail::SameText(kind, name))
								classes |= bit;
					}
				return classes;
			};
			if (const Node *weapon = module.block->Find("WeaponTemplate"))
				combat.pointDefenseWeapon = std::string(weapon->Value());
			combat.pointDefensePrimary = kinds("PrimaryTargetTypes");
			combat.pointDefenseSecondary = kinds("SecondaryTargetTypes");
			if (const auto rate = fixed(module.block->Find("ScanRate")))
				combat.pointDefenseScanTicks = static_cast<std::uint32_t>(detail::Ticks(rate->Ceil(), step));
			if (const auto range = fixed(module.block->Find("ScanRange")))
				combat.pointDefenseRange = *range;
		}
	// A later set with the same (no) conditions replaces an earlier one, as the original.
	const auto unconditioned = [](const Node *set) {
		if (set == nullptr)
			return false;
		const Node *conditions = set->Find("Conditions");
		return conditions == nullptr || conditions->values.empty() || detail::SameText(conditions->Value(), "None");
	};
	for (const Node *set : object.armorSets)
		if (unconditioned(set))
			if (const Node *armor = set->Find("Armor"))
				combat.armor = std::string(armor->Value());
	// A RiderChangeContain's initial payload rides it: the rider's model
	// condition shows it and the rider's weapon set arms it.
	std::string weaponCondition;
	for (const ModuleEntry &module : object.modules)
		if (module.block != nullptr && module.type == "RiderChangeContain")
			if (const Node *payload = module.block->Find("InitialPayload"))
				for (const Node &child : module.block->children)
					if (child.key.starts_with("Rider") && child.values.size() >= 3 && detail::SameText(child.Value(0), payload->Value(0)))
					{
						combat.riderCondition = std::string(child.Value(1));
						weaponCondition = std::string(child.Value(2));
						combat.riderWeaponCondition = weaponCondition;
						if (child.values.size() >= 4)
							combat.riderStatus = std::string(child.Value(3));
					}
	const auto only = [](const Node *set, std::string_view condition) {
		const Node *conditions = set != nullptr ? set->Find("Conditions") : nullptr;
		return conditions != nullptr && conditions->values.size() == 1 && detail::SameText(conditions->Value(), condition);
	};
	bool riderSet = false;
	for (const Node *set : object.weaponSets)
		if ((!riderSet && unconditioned(set)) || (!weaponCondition.empty() && only(set, weaponCondition)))
		{
			riderSet = riderSet || !unconditioned(set);
			combat.primaryWeapon.clear();
			combat.slotWeapons = {};
			for (const Node &child : set->children)
				if (detail::SameText(child.key, "Weapon") && !detail::SameText(child.Value(1), "None"))
				{
					const std::string_view slot = child.Value(0);
					const std::size_t index = detail::SameText(slot, "PRIMARY") ? 0 : detail::SameText(slot, "SECONDARY") ? 1 : detail::SameText(slot, "TERTIARY") ? 2 : 3;
					if (index < 3)
						combat.slotWeapons[index] = std::string(child.Value(1));
				}
			combat.primaryWeapon = combat.slotWeapons[0];
			combat.slotRules = ReadSlotRules(*set);
		}
	return combat;
}

// An object that fires a weapon at itself (FireWeaponUpdate): the weapon's
// name, the delay before its first shot and how long its own weapons firing
// hold it back (ticks).
struct ObjectAutoFire
{
	std::string weapon;
	std::uint64_t initialDelay{0};
	std::uint64_t exclusiveDelay{0};
};

std::optional<ObjectAutoFire> ReadObjectAutoFire(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || module.type != "FireWeaponUpdate")
			continue;
		const engine::config::Node *weapon = module.block->Find("Weapon");
		if (weapon == nullptr || weapon->Value().empty() || detail::SameText(weapon->Value(), "None"))
			continue;
		engine::config::Diagnostics diagnostics;
		engine::config::BindContext bind{diagnostics, step};
		const auto ticks = [&](std::string_view key) -> std::uint64_t {
			const auto *node = module.block->Find(key);
			return node != nullptr ? engine::config::ReadDurationTicks(*node, bind).value_or(0) : 0;
		};
		return ObjectAutoFire{std::string(weapon->Value()), ticks("InitialDelay"), ticks("ExclusiveWeaponDelay")};
	}
	return std::nullopt;
}
// FireWeaponWhenDamagedBehavior: the reaction and continuous weapons by damage state (pristine, damaged, really damaged,
// rubble; empty: none), whether it starts active (StartsActive; else its upgrade), the damage types it reacts to
// (INI::parseDamageTypeFlags: ALL, NONE, +TYPE, -TYPE; ALL by default) and how much a hit must do (DamageAmount, 0).
struct DamageReactionContent
{
	std::array<std::string, 4> reaction;
	std::array<std::string, 4> continuous;
	bool startsActive{false};
	std::uint64_t damageTypes{~std::uint64_t{0}};
	Engine::Math::Fixed threshold;
};

inline std::optional<DamageReactionContent> ReadDamageReaction(const ObjectDefinition &object)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "FireWeaponWhenDamagedBehavior")
			continue;
		DamageReactionContent content;
		static constexpr std::array<std::string_view, 4> states{"Pristine", "Damaged", "ReallyDamaged", "Rubble"};
		for (std::size_t state = 0; state < states.size(); ++state)
		{
			if (const engine::config::Node *weapon = module.block->Find("ReactionWeapon" + std::string(states[state])); weapon != nullptr && !weapon->values.empty())
				content.reaction[state] = std::string(weapon->Value());
			if (const engine::config::Node *weapon = module.block->Find("ContinuousWeapon" + std::string(states[state])); weapon != nullptr && !weapon->values.empty())
				content.continuous[state] = std::string(weapon->Value());
		}
		if (const engine::config::Node *active = module.block->Find("StartsActive"))
			content.startsActive = engine::config::values::ParseBool(active->Value()).value_or(false);
		if (const engine::config::Node *amount = module.block->Find("DamageAmount"))
			content.threshold = engine::config::values::ParseFixed(amount->Value()).value_or(Engine::Math::Fixed{});
		if (const engine::config::Node *types = module.block->Find("DamageTypes"))
			content.damageTypes = ParseDamageTypeFlags(*types);
		return content;
	}
	return std::nullopt;
}

}
