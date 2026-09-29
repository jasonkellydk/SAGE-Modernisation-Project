export module games.generalszh.content.containment.transport_content;
import std;
import games.generalszh.content.combat.combat_catalog;

export import engine.gameplay.rts.containment.definitions.transport;
export import games.generalszh.content.objects.object_definition;

// An object's transport, from its TransportContain-family module: "Slots"
// (TransportContain and kin) or "ContainMax" (the open containers), and
// "ExitDelay" in milliseconds, and whether passengers may fire from inside
// ("PassengersAllowedToFire"; TransportContain::isPassengerAllowedToFire: infantry only).
export namespace generalszh::content
{
// The contain module that makes it a transport: one with Slots or ContainMax (not a parachute), its own over a copied one.
inline const ModuleEntry *TransportModule(const ObjectDefinition &object)
{
	const ModuleEntry *contain = nullptr;
	for (const ModuleEntry &module : object.modules)
	{
		const std::string_view type = module.type;
		if (module.block == nullptr || !type.ends_with("Contain") || type == "ParachuteContain")
			continue;
		if (module.block->Find("Slots") == nullptr && module.block->Find("ContainMax") == nullptr)
			continue;
		if (contain == nullptr || (contain->copied && !module.copied))
			contain = &module;
	}
	return contain;
}

// OpenContain::isValidContainerFor: whether `rider` may go into `container` by its kind (AllowInsideKindOf: any of
// these, all when not given; ForbidInsideKindOf: none of these) and by how it stands to the container
// (AllowAlliesInside / AllowEnemiesInside / AllowNeutralInside, all Yes by default; `relation`: 0 allies, 1 enemies,
// 2 neutral).
inline bool AllowsInside(const ObjectDefinition &container, const ObjectDefinition &rider, int relation)
{
	const ModuleEntry *contain = TransportModule(container);
	if (contain == nullptr)
		return false;
	const auto anyOf = [&](const char *key, bool none) {
		const auto *node = contain->block->Find(key);
		if (node == nullptr)
			return none;
		for (const std::string_view kind : node->values)
			if (rider.Is(std::string(kind)))
				return true;
		return false;
	};
	if (!anyOf("AllowInsideKindOf", true) || anyOf("ForbidInsideKindOf", false))
		return false;
	const char *keys[] = {"AllowAlliesInside", "AllowEnemiesInside", "AllowNeutralInside"};
	if (const auto *node = contain->block->Find(keys[std::clamp(relation, 0, 2)]))
		return !node->Value().empty() && (node->Value()[0] == 'Y' || node->Value()[0] == 'y');
	return true;
}

std::optional<engine::gameplay::TransportDefinition> ReadObjectTransport(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	const ModuleEntry *contain = TransportModule(object);
	if (contain == nullptr)
		return std::nullopt;
	const auto integer = [&](std::string_view key) -> std::optional<std::int64_t> {
		const auto *node = contain->block->Find(key);
		if (node == nullptr)
			return std::nullopt;
		const auto value = engine::config::values::ParseFixed(node->Value());
		return value ? std::optional(value->Floor()) : std::nullopt;
	};
	engine::gameplay::TransportDefinition transport;
	transport.slots = static_cast<std::uint32_t>(std::max<std::int64_t>(0, integer("Slots").value_or(integer("ContainMax").value_or(0))));
	if (transport.slots == 0)
		return std::nullopt;
	const std::int64_t exitMilliseconds = integer("ExitDelay").value_or(0);
	transport.exitDelay = exitMilliseconds <= 0 ? 0
		: static_cast<std::uint64_t>((exitMilliseconds * static_cast<std::int64_t>(step.TicksPerSecond()) + 999) / 1000);
	// DoorOpenTime: parseDurationUnsignedInt (ms, up to whole frames); OpenContainModuleData's default one frame.
	if (const auto door = integer("DoorOpenTime"))
		transport.doorOpenTicks = *door <= 0 ? 0 : static_cast<std::uint64_t>((*door * static_cast<std::int64_t>(step.TicksPerSecond()) + 999) / 1000);
	if (const auto *passed = contain->block->Find("WeaponBonusPassedToPassengers"))
		transport.bonusToPassengers = engine::config::values::ParseBool(passed->Value()).value_or(false);
	if (const auto *fire = contain->block->Find("PassengersAllowedToFire"))
	{
		const std::string_view value = fire->Value();
		transport.passengersFire = !value.empty() && (value[0] == 'Y' || value[0] == 'y');
	}
	const std::string_view type = contain->type;
	transport.infantryOnly = type == "TransportContain" || type == "HelixContain" || type == "OverlordContain";
	if (const auto *allowed = contain->block->Find("AllowInsideKindOf"))
		for (const std::string_view kind : allowed->values)
			transport.mountsPortable = transport.mountsPortable || kind == "PORTABLE_STRUCTURE";
	// HelixContain::redeployOccupants (8 above it) and onContaining (held, garrisoned).
	if (type == "HelixContain")
	{
		transport.riderHeight = Engine::Math::Fixed::FromInt(8);
		transport.garrisonsRiders = true;
	}
	// GarrisonContain::isPassengerAllowedToFire: a garrison always lets them shoot out.
	if (type == "GarrisonContain")
		transport.passengersFire = true;
	// Its riders as it dies (OpenContain::onDie): DamagePercentToUnits of their maximum, unresistable, dying BURNED
	// unless BurnedDeathToUnits = No (the default is Yes); the TransportContain family (Helix, Overlord, rider-change,
	// railed, internet hack) first kills those that may not get out (or deletes them: DestroyRidersWhoAreNotFreeToExit),
	// and heals its riders HealthRegen%PerSec of their maximum a second.
	const auto yes = [&](std::string_view key, bool fallback) {
		const auto *node = contain->block->Find(key);
		if (node == nullptr)
			return fallback;
		const std::string_view value = node->Value();
		return !value.empty() && (value[0] == 'Y' || value[0] == 'y');
	};
	if (type == "OverlordContain")
	{
		transport.experienceSink = yes("ExperienceSinkForRider", true);
		transport.redirectsToMount = true;
	}
	if (const auto *damage = contain->block->Find("DamagePercentToUnits"))
	{
		engine::config::Diagnostics diagnostics;
		engine::config::BindContext bind{diagnostics, step};
		transport.riderDamage = engine::config::ReadPercent(*damage, bind).value_or(Engine::Math::Fixed{});
	}
	transport.riderDamageType = DamageTypeIndex("UNRESISTABLE").value_or(0);
	transport.stuckDeathType = DeathTypeIndex("NORMAL").value_or(0);
	transport.riderDeathType = yes("BurnedDeathToUnits", true) ? DeathTypeIndex("BURNED").value_or(0) : transport.stuckDeathType;
	transport.checksRiderExit = type == "TransportContain" || type == "HelixContain" || type == "OverlordContain" || type == "RiderChangeContain" ||
		type == "RailedTransportContain" || type == "InternetHackContain";
	transport.deletesRiders = type == "RiderChangeContain";
	if (transport.checksRiderExit)
	{
		transport.deletesStuckRiders = yes("DestroyRidersWhoAreNotFreeToExit", false);
		transport.goAggressiveOnExit = yes("GoAggressiveOnExit", false);
		if (const auto *regen = contain->block->Find("HealthRegen%PerSec"))
			transport.riderRegen = engine::config::values::ParseFixed(regen->Value()).value_or(Engine::Math::Fixed{}) /
				Engine::Math::Fixed::FromInt(100 * static_cast<std::int64_t>(step.TicksPerSecond()));
	}
	return transport;
}

// A TunnelContain: a way into its player's tunnel network, healing those in it over TimeForFullHeal (ms, in frames:
// ms * 30 / 1000, at least one).
inline std::optional<std::uint64_t> ReadObjectTunnel(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "TunnelContain")
			continue;
		Engine::Math::Fixed ms;
		if (const auto *time = module.block->Find("TimeForFullHeal"))
			ms = engine::config::values::ParseFixed(time->Value()).value_or(Engine::Math::Fixed{});
		return static_cast<std::uint64_t>(std::max<std::int64_t>(
			(ms * Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(step.TicksPerSecond())) / Engine::Math::Fixed::FromInt(1000)).Floor(), 1));
	}
	return std::nullopt;
}

// A GarrisonContain: whether it heals those inside (HealObjects) over TimeForFullHeal (ms: ms * 30 / 1000 frames).
struct GarrisonContent
{
	std::uint64_t fullHealTicks{0};
	bool untilDestroyed{false};
	bool immuneToClear{false}; // ImmuneToClearBuildingAttacks
	std::string rosterObject; // InitialRoster = object count
	std::uint32_t rosterCount{0};
};

inline std::optional<GarrisonContent> ReadObjectGarrison(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "GarrisonContain")
			continue;
		GarrisonContent garrison;
		garrison.untilDestroyed = object.Is("GARRISONABLE_UNTIL_DESTROYED");
		if (const auto *immune = module.block->Find("ImmuneToClearBuildingAttacks"))
			garrison.immuneToClear = engine::config::values::ParseBool(immune->Value()).value_or(false);
		if (const auto *roster = module.block->Find("InitialRoster"); roster != nullptr && roster->values.size() >= 2)
		{
			garrison.rosterObject = std::string(roster->Value(0));
			garrison.rosterCount = static_cast<std::uint32_t>(std::max<std::int64_t>(engine::config::values::ParseInt(roster->Value(1)).value_or(0), 0));
		}
		const auto *heals = module.block->Find("HealObjects");
		if (heals != nullptr && !heals->Value().empty() && (heals->Value()[0] == 'Y' || heals->Value()[0] == 'y'))
			if (const auto *time = module.block->Find("TimeForFullHeal"))
			{
				const auto ms = engine::config::values::ParseFixed(time->Value()).value_or(Engine::Math::Fixed{});
				garrison.fullHealTicks = static_cast<std::uint64_t>(std::max<std::int64_t>(
					(ms * Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(step.TicksPerSecond())) / Engine::Math::Fixed::FromInt(1000)).Floor(), 1));
			}
		return garrison;
	}
	return std::nullopt;
}
// A HealContain: those inside heal over TimeForFullHeal (INI::parseDurationUnsignedInt: ms in frames, rounded up; at
// least one) and leave once whole.
inline std::optional<std::uint64_t> ReadObjectHealPad(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "HealContain")
			continue;
		Engine::Math::Fixed ms;
		if (const auto *time = module.block->Find("TimeForFullHeal"))
			ms = engine::config::values::ParseFixed(time->Value()).value_or(Engine::Math::Fixed{});
		return static_cast<std::uint64_t>(std::max<std::int64_t>(
			(ms * Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(step.TicksPerSecond())) / Engine::Math::Fixed::FromInt(1000)).Ceil(), 1));
	}
	return std::nullopt;
}

// AutoFindHealingUpdate: a computer player's hurt, idle unit looks every ScanRate (ms in frames, rounded up) within
// ScanRange for the nearest heal pad and goes to be healed there, unless it is healthier than NeverHeal (default
// 0.95) of its maximum. (AlwaysHeal is never used: update returns before it.)
struct AutoFindHealingContent
{
	std::uint64_t scanTicks{0};
	Engine::Math::Fixed range;
	Engine::Math::Fixed neverHeal;
};

inline std::optional<AutoFindHealingContent> ReadAutoFindHealing(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "AutoFindHealingUpdate")
			continue;
		const auto fixed = [&](std::string_view key, Engine::Math::Fixed fallback) {
			const auto *node = module.block->Find(key);
			return node != nullptr ? engine::config::values::ParseFixed(node->Value()).value_or(fallback) : fallback;
		};
		AutoFindHealingContent healing;
		healing.scanTicks = static_cast<std::uint64_t>(std::max<std::int64_t>(0,
			(fixed("ScanRate", {}) * Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(step.TicksPerSecond())) / Engine::Math::Fixed::FromInt(1000)).Ceil()));
		healing.range = fixed("ScanRange", {});
		healing.neverHeal = fixed("NeverHeal", Engine::Math::Fixed::FromRatio(95, 100));
		return healing;
	}
	return std::nullopt;
}
}

export namespace generalszh::content
{
// RiderChangeContain: each rider it may carry (Rider1..Rider8: the template, then the model condition, weapon set flag
// and object status it gives the vehicle, its command set and locomotor set), ScuttleDelay (ms, in frames) and
// ScuttleStatus (the model condition it shows while it is scuttled; TOPPLED by default).
struct RiderChangeRider
{
	std::string name;
	std::string condition;
	std::string weaponFlag;
	std::string status;
};

struct RiderChangeContent
{
	std::vector<RiderChangeRider> riders;
	std::uint64_t scuttleTicks{0};
	std::string scuttleCondition{"TOPPLED"};
};

inline std::optional<RiderChangeContent> ReadRiderChange(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.type != "RiderChangeContain" || module.block == nullptr)
			continue;
		RiderChangeContent content;
		for (int index = 1; index <= 8; ++index)
			if (const auto *rider = module.block->Find("Rider" + std::to_string(index)); rider != nullptr && rider->values.size() >= 4)
				content.riders.push_back({std::string(rider->Value(0)), std::string(rider->Value(1)), std::string(rider->Value(2)), std::string(rider->Value(3))});
		if (const auto *delay = module.block->Find("ScuttleDelay"))
		{
			const auto ms = engine::config::values::ParseInt(delay->Value()).value_or(0);
			content.scuttleTicks = ms <= 0 ? 0 : static_cast<std::uint64_t>((ms * static_cast<std::int64_t>(step.TicksPerSecond()) + 999) / 1000);
		}
		if (const auto *state = module.block->Find("ScuttleStatus"))
			content.scuttleCondition = std::string(state->Value());
		return content;
	}
	return std::nullopt;
}
}
