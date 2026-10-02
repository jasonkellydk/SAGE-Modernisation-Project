export module games.generalszh.content.stealth.stealth_content;
import std;

export import engine.gameplay.rts.stealth.components.stealth;
export import engine.gameplay.rts.stealth.components.stealth_detector;
export import engine.gameplay.rts.stealth.components.grant_stealth;
import engine.gameplay.common.spatial.components.targetable;
export import games.generalszh.content.objects.object_definition;

// An object's stealth, from its StealthUpdate module: whether it is born
// able to stealth (InnateStealth), what keeps it from stealthing
// (StealthForbiddenConditions), its delay and moving threshold; and how
// friends see it while stealthed (opacity pulsing between FriendlyOpacityMin
// and Max every PulseFrequency). And an object's stealth detector, from its
// StealthDetectorUpdate: how often it scans (DetectionRate), how far
// (DetectionRange, else its vision range), what kinds it may find
// (ExtraRequiredKindOf / ExtraForbiddenKindOf), whether it starts off
// (InitiallyDisabled) and whether it detects from inside a building or a
// transport.
export namespace generalszh::content
{
struct ObjectStealth
{
	engine::gameplay::Stealth stealth;
	Engine::Math::Fixed friendlyOpacityMin{Engine::Math::Fixed::FromRatio(1, 2)};
	Engine::Math::Fixed friendlyOpacityMax{Engine::Math::Fixed::One()};
	std::uint64_t pulseTicks{30};
	// The disguise's FX lists (DisguiseFX, DisguiseRevealFX) and the EVA events of its detection
	// (EnemyDetectionEvaEvent for the detector's side, OwnDetectionEvaEvent for its own): names, none empty.
	std::string disguiseFx;
	std::string disguiseRevealFx;
	std::string enemyDetectionEva;
	std::string ownDetectionEva;
};

namespace stealth_detail
{
inline constexpr std::array<std::string_view, 9> ForbiddenNames{"ATTACKING", "MOVING", "USING_ABILITY", "FIRING_PRIMARY",
	"FIRING_SECONDARY", "FIRING_TERTIARY", "NO_BLACK_MARKET", "TAKING_DAMAGE", "RIDERS_ATTACKING"};

bool Same(std::string_view a, std::string_view b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
	});
}

// KindOf names to the engine's target classes.
std::uint32_t Classes(const engine::config::Node &field)
{
	namespace target = engine::gameplay::target_class;
	constexpr std::pair<std::string_view, std::uint32_t> kinds[] = {{"INFANTRY", target::Infantry}, {"VEHICLE", target::Vehicle},
		{"AIRCRAFT", target::Aircraft}, {"STRUCTURE", target::Structure}, {"PROJECTILE", target::Projectile}, {"MINE", target::Mine}};
	std::uint32_t bits = 0;
	for (const std::string_view token : field.values)
		for (const auto &[name, bit] : kinds)
			if (Same(token, name))
				bits |= bit;
	return bits;
}
}

std::optional<ObjectStealth> ReadObjectStealth(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	using namespace stealth_detail;
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || module.type != "StealthUpdate")
			continue;
		const engine::config::Node &block = *module.block;
		engine::config::Diagnostics diagnostics;
		engine::config::BindContext bind{diagnostics, step};
		ObjectStealth result;
		auto &stealth = result.stealth;
		// The original's defaults: innate; no StealthDelay is UINT_MAX frames, which the original adds to the frame in 32
		// bits (m_stealthAllowedFrame = now + delay, m_detectionExpiresFrame = now + delay) and so wraps to the frame
		// before: stealthed at once, and never detected by a markAsDetected of its own delay. A delay of 0 does exactly that.
		stealth.delay = 0;
		bool innate = true;
		const auto text = [](const engine::config::Node &field) { return std::string(field.Value()); };
		const auto option = [&](const engine::config::Node &field, std::uint32_t bit) {
			const bool on = engine::config::ReadBool(field, bind).value_or(false);
			stealth.options = on ? (stealth.options | bit) : (stealth.options & ~bit);
		};
		for (const engine::config::Node &field : block.children)
		{
			const std::string_view key = field.key;
			if (Same(key, "InnateStealth"))
				innate = engine::config::ReadBool(field, bind).value_or(true);
			else if (Same(key, "StealthDelay"))
				stealth.delay = engine::config::ReadDurationTicks(field, bind).value_or(stealth.delay);
			else if (Same(key, "MoveThresholdSpeed"))
				stealth.moveThreshold = engine::config::ReadPerSecond(field, bind).value_or(Engine::Math::Fixed{});
			else if (Same(key, "StealthForbiddenConditions"))
			{
				stealth.forbidden = 0;
				for (const std::string_view token : field.values)
					for (std::size_t index = 0; index < ForbiddenNames.size(); ++index)
						if (Same(token, ForbiddenNames[index]))
							stealth.forbidden |= 1u << index;
			}
			else if (Same(key, "HintDetectableConditions"))
			{
				stealth.hint = 0;
				for (const std::string_view token : field.values)
					stealth.hint |= Same(token, "IS_FIRING_WEAPON") ? engine::gameplay::stealth_hint::FiringWeapon
						: Same(token, "USING_ABILITY")               ? engine::gameplay::stealth_hint::UsingAbility
																	 : 0u;
			}
			else if (Same(key, "FriendlyOpacityMin"))
				result.friendlyOpacityMin = engine::config::ReadPercent(field, bind).value_or(result.friendlyOpacityMin);
			else if (Same(key, "FriendlyOpacityMax"))
				result.friendlyOpacityMax = engine::config::ReadPercent(field, bind).value_or(result.friendlyOpacityMax);
			else if (Same(key, "PulseFrequency"))
				result.pulseTicks = std::max<std::uint64_t>(1, engine::config::ReadDurationTicks(field, bind).value_or(30));
			else if (Same(key, "DisguisesAsTeam"))
				option(field, engine::gameplay::stealth_option::DisguisesAsTeam);
			else if (Same(key, "OrderIdleEnemiesToAttackMeUponReveal"))
				option(field, engine::gameplay::stealth_option::OrderIdleEnemies);
			else if (Same(key, "GrantedBySpecialPower"))
				option(field, engine::gameplay::stealth_option::GrantedBySpecialPower);
			else if (Same(key, "UseRiderStealth"))
				option(field, engine::gameplay::stealth_option::UseRiderStealth);
			else if (Same(key, "RevealDistanceFromTarget"))
				stealth.revealDistance = engine::config::ReadFixed(field, bind).value_or(Engine::Math::Fixed{});
			else if (Same(key, "DisguiseTransitionTime"))
				stealth.disguiseTicks = static_cast<std::uint32_t>(engine::config::ReadDurationTicks(field, bind).value_or(0));
			else if (Same(key, "DisguiseRevealTransitionTime"))
				stealth.revealTicks = static_cast<std::uint32_t>(engine::config::ReadDurationTicks(field, bind).value_or(0));
			else if (Same(key, "DisguiseFX"))
				result.disguiseFx = text(field);
			else if (Same(key, "DisguiseRevealFX"))
				result.disguiseRevealFx = text(field);
			else if (Same(key, "EnemyDetectionEvaEvent"))
				result.enemyDetectionEva = text(field);
			else if (Same(key, "OwnDetectionEvaEvent"))
				result.ownDetectionEva = text(field);
		}
		if (innate)
			stealth.flags |= engine::gameplay::stealth_flag::CanStealth;
		// A disguiser's update starts off (m_enabled = !m_teamDisguised) till it takes a disguise; a grant's sleeps till it
		// is granted (setWakeFrame UPDATE_SLEEP_FOREVER).
		if (stealth.Option(engine::gameplay::stealth_option::DisguisesAsTeam))
			stealth.flags |= engine::gameplay::stealth_flag::Off;
		if (stealth.Option(engine::gameplay::stealth_option::GrantedBySpecialPower))
			stealth.flags |= engine::gameplay::stealth_flag::Asleep;
		return result;
	}
	return std::nullopt;
}

std::optional<engine::gameplay::StealthDetector> ReadObjectStealthDetector(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	using namespace stealth_detail;
	namespace gameplay = engine::gameplay;
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || module.type != "StealthDetectorUpdate")
			continue;
		engine::config::Diagnostics diagnostics;
		engine::config::BindContext bind{diagnostics, step};
		gameplay::StealthDetector detector;
		detector.range = object.visionRange;
		detector.flags = gameplay::stealth_detector_flag::Enabled;
		const auto flag = [&](const engine::config::Node &field, std::uint32_t bit) {
			const bool on = engine::config::ReadBool(field, bind).value_or(false);
			detector.flags = on ? (detector.flags | bit) : (detector.flags & ~bit);
		};
		for (const engine::config::Node &field : module.block->children)
		{
			const std::string_view key = field.key;
			if (Same(key, "DetectionRate"))
				detector.rate = std::max<std::uint64_t>(1, engine::config::ReadDurationTicks(field, bind).value_or(1));
			else if (Same(key, "DetectionRange"))
			{
				const auto range = engine::config::ReadFixed(field, bind).value_or(Engine::Math::Fixed{});
				if (range > Engine::Math::Fixed{})
				{
					detector.range = range;
					detector.flags |= gameplay::stealth_detector_flag::OwnRange;
				}
			}
			else if (Same(key, "InitiallyDisabled"))
				detector.flags = engine::config::ReadBool(field, bind).value_or(false) ? (detector.flags & ~gameplay::stealth_detector_flag::Enabled)
																					   : (detector.flags | gameplay::stealth_detector_flag::Enabled);
			else if (Same(key, "CanDetectWhileGarrisoned"))
				flag(field, gameplay::stealth_detector_flag::WhileGarrisoned);
			else if (Same(key, "CanDetectWhileContained"))
				flag(field, gameplay::stealth_detector_flag::WhileContained);
			else if (Same(key, "ExtraRequiredKindOf"))
				detector.requiredClasses = Classes(field);
			else if (Same(key, "ExtraForbiddenKindOf"))
				detector.forbiddenClasses = Classes(field);
		}
		return detector;
	}
	return std::nullopt;
}

// A stealth grantor (GrantStealthBehavior): StartRadius growing by
// RadiusGrowRate each frame up to FinalRadius, granting to KindOf.
std::optional<engine::gameplay::GrantStealth> ReadObjectGrantStealth(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	using namespace stealth_detail;
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || module.type != "GrantStealthBehavior")
			continue;
		engine::config::Diagnostics diagnostics;
		engine::config::BindContext bind{diagnostics, step};
		// The original's defaults.
		engine::gameplay::GrantStealth grant{Engine::Math::Fixed::FromInt(1), Engine::Math::Fixed::FromInt(1), Engine::Math::Fixed::FromInt(200), 0, 0};
		for (const engine::config::Node &field : module.block->children)
		{
			const std::string_view key = field.key;
			if (Same(key, "StartRadius"))
				grant.radius = engine::config::ReadFixed(field, bind).value_or(grant.radius);
			else if (Same(key, "FinalRadius"))
				grant.finalRadius = engine::config::ReadFixed(field, bind).value_or(grant.finalRadius);
			else if (Same(key, "RadiusGrowRate"))
				grant.growRate = engine::config::ReadFixed(field, bind).value_or(grant.growRate);
			else if (Same(key, "KindOf"))
				grant.classes = Classes(field);
		}
		return grant;
	}
	return std::nullopt;
}

// How a stealth detector's scans show and sound (StealthDetectorUpdate):
// each scan an IR ping (bright when it found something) and a beacon at its
// bone, a ping sound (loud when it found something), and an IR grid under
// each thing it revealed.
struct DetectorLook
{
	std::string ping;
	std::string brightPing;
	std::string beacon;
	std::string grid;
	std::string bone;
	std::string pingSound;
	std::string loudPingSound;
};

std::optional<DetectorLook> ReadDetectorLook(const ObjectDefinition &object)
{
	using namespace stealth_detail;
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || module.type != "StealthDetectorUpdate")
			continue;
		DetectorLook look;
		const auto text = [&](std::string_view key) {
			const auto *node = module.block->Find(key);
			return node != nullptr ? std::string(node->Value()) : std::string{};
		};
		look.ping = text("IRParticleSysName");
		look.brightPing = text("IRBrightParticleSysName");
		look.beacon = text("IRBeaconParticleSysName");
		look.grid = text("IRGridParticleSysName");
		look.bone = text("IRParticleSysBone");
		look.pingSound = text("PingSound");
		look.loudPingSound = text("LoudPingSound");
		return look;
	}
	return std::nullopt;
}
}
