export module games.generalszh.content.powers.special_powers;
import std;

export import engine.config.binding.schema;
export import games.generalszh.content.objects.object_definition;

// Special powers as content: which power a command button fires
// ("CommandButton" blocks, Data/INI/CommandButton.ini), which object
// creation list an object's OCLSpecialPower module runs for a power, and the
// payload runs ("DeliverPayload" nuggets) of those lists
// (Data/INI/ObjectCreationList.ini).
export namespace generalszh::content
{
struct PayloadItem
{
	std::string object;
	std::uint32_t count{1};
};

struct DeliveryNugget
{
	std::string transport;
	std::vector<PayloadItem> payload;
	std::string putInContainer;
	bool parachuteDirectly{false}; // its parachutes land on the target (setOverrideDestination)
	Engine::Math::Fixed deliveryDistance;
	Engine::Math::Fixed preOpenDistance;    // PreOpenDistance: inbound, it may start this much further out
	std::int32_t maxAttempts{1};             // MaxAttempts
	Engine::Math::FixedVector3 dropOffset;   // DropOffset
	Engine::Math::FixedVector3 dropVariance; // DropVariance
	bool selfDestruct{false};                // SelfDestructObject
	std::uint64_t dropDelay{0}; // ticks
	std::uint32_t formationSize{1};
	Engine::Math::Fixed formationSpacing{Engine::Math::Fixed::FromInt(25)};
	Engine::Math::Fixed convergenceFactor; // WeaponConvergenceFactor
	Engine::Math::Fixed errorRadius;       // WeaponErrorRadius
	std::uint64_t delayDeliveryMax{0};     // DelayDeliveryMax, ticks
	bool startAtPreferredHeight{true};
	bool startAtMaxSpeed{false};
};

enum class CreateLocation : std::uint8_t
{
	AtEdgeNearSource,
	AtEdgeNearTarget,
	AtLocation,
	UseOwnerObject,
	AboveLocation,
	AtEdgeFarthestFromTarget,
};

struct OclPower
{
	std::string creationList;
	CreateLocation location{CreateLocation::AtEdgeNearSource};
};

// A SpecialPower block (SpecialPowerTemplate): its type (Enum), how long it recharges (ReloadTime, in ticks), the
// science its player needs to use it, whether its timer is shown to all (PublicTimer) or shared by all the player's
// objects with it (SharedSyncedTimer: the general's powers), and whether it may be fired from anywhere (ShortcutPower).
struct SpecialPowerTemplate
{
	std::string name;
	std::string type;
	std::uint64_t reloadTicks{0};
	std::string requiredScience;
	bool publicTimer{false};
	bool sharedSynced{false};
	bool shortcut{false};
	Engine::Math::Fixed radiusCursorRadius; // RadiusCursorRadius
	// The look its firing leaves where it lands (ViewObjectDuration, rounded up to ticks; ViewObjectRange): none unless both.
	std::uint64_t viewObjectTicks{0};
	Engine::Math::Fixed viewObjectRange;
	std::string initiateSound;           // InitiateSound: on the source as it fires
	std::string initiateAtLocationSound; // InitiateAtLocationSound: where it lands
	std::uint64_t detectionTicks{30 * 10}; // DetectionTime (ms rounded up; default 10 seconds): how long a defector hides
};

// An object's special power module (any behaviour naming a SpecialPowerTemplate: OCLSpecialPower, SpecialAbility,
// CashHackSpecialPower, ...): the power, whether its countdown starts paused (StartsPaused: an upgrade starts it),
// whether only scripts fire it (ScriptedSpecialPowerOnly), and whether its update module fires it
// (UpdateModuleStartsAttack).
struct PowerModule
{
	std::string type;
	std::string power;
	bool startsPaused{false};
	bool scriptOnly{false};
	bool updateModuleStartsAttack{false};
	std::uint32_t maxShotsToFire{1}; // FireWeaponPower MaxShotsToFire
	Engine::Math::Fixed moveRange;   // CleanupAreaPower MaxMoveDistanceFromLocation
	// SpyVisionSpecialPower BaseDuration, BonusDurationPerCaptured, MaxDuration (ms rounded up to ticks).
	std::uint64_t baseDurationTicks{0};
	std::uint64_t bonusDurationTicks{0};
	std::uint64_t maxDurationTicks{0};
	std::string detonationObject; // BaikonurLaunchPower DetonationObject
};

// A SpyVisionUpdate module: whether an upgrade turns it on (NeedsUpgrade), whether it turns itself on and off on its
// own timers (SelfPowered: on for SelfPoweredDuration, off for SelfPoweredInterval; ms rounded up to ticks, 0: always),
// and the kinds of the enemy's things it sees through (SpyOnKindof; none given: everything).
struct SpyVisionModule
{
	bool needsUpgrade{false};
	bool selfPowered{false};
	std::uint64_t durationTicks{0};
	std::uint64_t intervalTicks{0};
	std::vector<std::string> kinds;
};

// CleanupHazardUpdate: how often (ScanRate, ms rounded up to ticks) and how far (ScanRange) it looks for hazards.
struct CleanupHazardModule
{
	std::uint64_t scanTicks{0};
	Engine::Math::Fixed scanRange;
};

struct SpecialPowerContent
{
	std::map<std::string, std::string, std::less<>> buttonPowers;                       // command button -> special power
	std::map<std::string, std::vector<DeliveryNugget>, std::less<>> deliveries;         // creation list -> payload runs
	std::vector<SpecialPowerTemplate> templates;                                         // SpecialPower.ini, in order

	std::optional<std::uint32_t> Template(std::string_view name) const
	{
		for (std::size_t index = 0; index < templates.size(); ++index)
			if (templates[index].name == name)
				return static_cast<std::uint32_t>(index);
		return std::nullopt;
	}
};

namespace detail
{
bool Same(std::string_view a, std::string_view b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
	});
}

Engine::Math::Fixed Fixed(const engine::config::Node *node)
{
	return node != nullptr ? engine::config::values::ParseFixed(node->Value()).value_or(Engine::Math::Fixed{}) : Engine::Math::Fixed{};
}

bool Yes(const engine::config::Node *node) { return node != nullptr && engine::config::values::ParseBool(node->Value()).value_or(false); }

// INI::parseCoord3D: "X:1 Y:2 Z:3".
Engine::Math::FixedVector3 Coord(const engine::config::Node &node)
{
	Engine::Math::FixedVector3 out{};
	for (const std::string_view token : node.values)
		if (token.size() > 2 && token[1] == ':')
			if (const auto value = engine::config::values::ParseFixed(token.substr(2)))
			{
				const char axis = static_cast<char>(std::toupper(static_cast<unsigned char>(token[0])));
				if (axis == 'X')
					out.x = *value;
				else if (axis == 'Y')
					out.y = *value;
				else if (axis == 'Z')
					out.z = *value;
			}
	return out;
}
}

SpecialPowerContent BindSpecialPowers(const engine::config::Document &commandButtons, const engine::config::Document &creationLists,
	const engine::time::FixedStep &step)
{
	SpecialPowerContent content;
	for (const engine::config::Node &root : commandButtons.Roots())
		if (root.key == "CommandButton")
			if (const auto *power = root.Find("SpecialPower"))
				content.buttonPowers.insert_or_assign(std::string(root.Value()), std::string(power->Value()));
	for (const engine::config::Node &root : creationLists.Roots())
	{
		if (root.key != "ObjectCreationList")
			continue;
		std::vector<DeliveryNugget> runs;
		for (const engine::config::Node &nugget : root.children)
		{
			if (nugget.key != "DeliverPayload")
				continue;
			DeliveryNugget run;
			// INI::parseDurationUnsignedInt: milliseconds, rounded up to whole ticks.
			const auto ticks = [&](const engine::config::Node &field) -> std::uint64_t {
				const std::int64_t milliseconds = detail::Fixed(&field).Ceil();
				return milliseconds <= 0 ? 0 : static_cast<std::uint64_t>((milliseconds * static_cast<std::int64_t>(step.TicksPerSecond()) + 999) / 1000);
			};
			for (const engine::config::Node &field : nugget.children)
			{
				if (field.key == "Transport")
					run.transport = std::string(field.Value());
				else if (field.key == "Payload")
				{
					const auto count = engine::config::values::ParseInt(field.Value(1));
					run.payload.push_back({std::string(field.Value(0)), static_cast<std::uint32_t>(std::max<std::int64_t>(1, count.value_or(1)))});
				}
				else if (field.key == "PutInContainer")
					run.putInContainer = std::string(field.Value());
				else if (field.key == "ParachuteDirectly")
					run.parachuteDirectly = detail::Yes(&field);
				else if (field.key == "DeliveryDistance")
					run.deliveryDistance = detail::Fixed(&field);
				else if (field.key == "DropDelay")
					run.dropDelay = ticks(field);
				else if (field.key == "PreOpenDistance")
					run.preOpenDistance = detail::Fixed(&field);
				else if (field.key == "MaxAttempts")
					run.maxAttempts = static_cast<std::int32_t>(engine::config::values::ParseInt(field.Value()).value_or(1));
				else if (field.key == "DropOffset")
					run.dropOffset = detail::Coord(field);
				else if (field.key == "DropVariance")
					run.dropVariance = detail::Coord(field);
				else if (field.key == "SelfDestructObject")
					run.selfDestruct = detail::Yes(&field);
				else if (field.key == "WeaponConvergenceFactor")
					run.convergenceFactor = detail::Fixed(&field);
				else if (field.key == "WeaponErrorRadius")
					run.errorRadius = detail::Fixed(&field);
				else if (field.key == "DelayDeliveryMax")
					run.delayDeliveryMax = ticks(field);
				else if (field.key == "FormationSize")
					run.formationSize = static_cast<std::uint32_t>(std::max<std::int64_t>(1, detail::Fixed(&field).Floor()));
				else if (field.key == "FormationSpacing")
					run.formationSpacing = detail::Fixed(&field);
				else if (field.key == "StartAtPreferredHeight")
					run.startAtPreferredHeight = detail::Yes(&field);
				else if (field.key == "StartAtMaxSpeed")
					run.startAtMaxSpeed = detail::Yes(&field);
			}
			if (!run.transport.empty())
				runs.push_back(std::move(run));
		}
		content.deliveries.insert_or_assign(std::string(root.Value()), std::move(runs));
	}
	return content;
}

// SpecialPower.ini: each block in order (a later block of a name changes the fields it names). ReloadTime is in
// milliseconds, rounded up to whole ticks (INI::parseDurationUnsignedInt).
std::vector<SpecialPowerTemplate> BindSpecialPowerTemplates(const engine::config::Document &document, const engine::time::FixedStep &step)
{
	std::vector<SpecialPowerTemplate> out;
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key != "SpecialPower" || root.values.empty())
			continue;
		const std::string name(root.Value());
		auto found = std::find_if(out.begin(), out.end(), [&](const SpecialPowerTemplate &power) { return power.name == name; });
		if (found == out.end())
		{
			out.push_back({name});
			found = out.end() - 1;
		}
		for (const engine::config::Node &field : root.children)
		{
			if (field.values.empty())
				continue;
			if (field.key == "Enum")
				found->type = std::string(field.Value());
			else if (field.key == "ReloadTime")
			{
				const Engine::Math::Fixed milliseconds = detail::Fixed(&field);
				found->reloadTicks = milliseconds <= Engine::Math::Fixed{} ? 0
					: static_cast<std::uint64_t>((milliseconds * Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(step.TicksPerSecond())) /
												   Engine::Math::Fixed::FromInt(1000)).Ceil());
			}
			else if (field.key == "DetectionTime")
			{
				const std::int64_t milliseconds = detail::Fixed(&field).Ceil();
				found->detectionTicks = milliseconds <= 0 ? 0 : static_cast<std::uint64_t>((milliseconds * static_cast<std::int64_t>(step.TicksPerSecond()) + 999) / 1000);
			}
			else if (field.key == "RequiredScience")
				found->requiredScience = field.Value() == "None" ? std::string{} : std::string(field.Value());
			else if (field.key == "PublicTimer")
				found->publicTimer = detail::Yes(&field);
			else if (field.key == "SharedSyncedTimer")
				found->sharedSynced = detail::Yes(&field);
			else if (field.key == "ShortcutPower")
				found->shortcut = detail::Yes(&field);
			else if (field.key == "RadiusCursorRadius")
				found->radiusCursorRadius = detail::Fixed(&field);
			else if (field.key == "ViewObjectDuration")
			{
				const Engine::Math::Fixed milliseconds = detail::Fixed(&field);
				found->viewObjectTicks = milliseconds <= Engine::Math::Fixed{} ? 0
					: static_cast<std::uint64_t>((milliseconds * Engine::Math::Fixed::FromInt(static_cast<std::int64_t>(step.TicksPerSecond())) /
												   Engine::Math::Fixed::FromInt(1000)).Ceil());
			}
			else if (field.key == "ViewObjectRange")
				found->viewObjectRange = detail::Fixed(&field);
			else if (field.key == "InitiateSound")
				found->initiateSound = std::string(field.Value());
			else if (field.key == "InitiateAtLocationSound")
				found->initiateAtLocationSound = std::string(field.Value());
		}
	}
	return out;
}

// The object's special power modules, in module order.
std::vector<PowerModule> PowerModulesOf(const ObjectDefinition &object)
{
	std::vector<PowerModule> out;
	for (const ModuleEntry &module : object.modules)
	{
		// The SpecialPowerModule kinds (their getSpecialPower); upgrades, updates and die modules that name a power are not.
		constexpr std::string_view kinds[] = {"SpecialPowerModule", "OCLSpecialPower", "SpecialAbility", "CashBountyPower", "CashHackSpecialPower",
			"CleanupAreaPower", "DefectorSpecialPower", "DemoralizeSpecialPower", "FireWeaponPower", "SpyVisionSpecialPower", "BaikonurLaunchPower"};
		if (module.block == nullptr || module.slot != ModuleSlot::Behavior || std::find(std::begin(kinds), std::end(kinds), module.type) == std::end(kinds))
			continue;
		const auto *power = module.block->Find("SpecialPowerTemplate");
		if (power == nullptr || power->values.empty())
			continue;
		PowerModule found{module.type, std::string(power->Value()), detail::Yes(module.block->Find("StartsPaused")),
			detail::Yes(module.block->Find("ScriptedSpecialPowerOnly")), detail::Yes(module.block->Find("UpdateModuleStartsAttack"))};
		found.moveRange = detail::Fixed(module.block->Find("MaxMoveDistanceFromLocation"));
		if (const auto *detonation = module.block->Find("DetonationObject"); detonation != nullptr && !detonation->values.empty())
			found.detonationObject = std::string(detonation->Value());
		const auto ticks = [&](std::string_view key) -> std::uint64_t {
			const std::int64_t milliseconds = detail::Fixed(module.block->Find(key)).Ceil();
			return milliseconds <= 0 ? 0 : static_cast<std::uint64_t>((milliseconds * 30 + 999) / 1000);
		};
		found.baseDurationTicks = ticks("BaseDuration");
		found.bonusDurationTicks = ticks("BonusDurationPerCaptured");
		found.maxDurationTicks = ticks("MaxDuration");
		if (const auto *shots = module.block->Find("MaxShotsToFire"))
			found.maxShotsToFire = static_cast<std::uint32_t>(std::max<std::int64_t>(0, engine::config::values::ParseInt(shots->Value()).value_or(1)));
		out.push_back(std::move(found));
	}
	return out;
}

// CountermeasuresBehavior: its flares (FlareTemplateName), VolleySize, VolleyArcAngle (degrees), VolleyVelocityFactor,
// DelayBetweenVolleys, NumberOfVolleys, ReloadTime, EvasionRate (percent), MustReloadAtAirfield, MissileDecoyDelay,
// ReactionLaunchLatency (durations in ms rounded up to ticks), and whether it is on from the start (the upgrade mux's
// StartsActive).
struct CountermeasuresModule
{
	std::string flare;
	std::uint32_t volleySize{0};
	Engine::Math::Fixed arcDegrees;
	Engine::Math::Fixed velocityFactor;
	std::uint64_t volleyTicks{0};
	std::uint32_t volleys{0};
	std::uint64_t reloadTicks{0};
	Engine::Math::Fixed evasionRate;
	bool mustReloadAtAirfield{false};
	std::uint64_t decoyTicks{0};
	std::uint64_t reactionTicks{0};
	bool startsActive{false};
};

// ParticleUplinkCannonUpdate: its power, times (ms up to whole ticks), the beam's sweep, marks, damage and driving,
// its effects, its beam object (ParticleBeamLaserName: its W3DLaserDraw's OuterBeamWidth halved is the beam's radius)
// and the remnant each pulse leaves.
struct ParticleCannonModule
{
	std::string power;
	std::uint64_t beginChargeTicks{0}, raiseAntennaTicks{0}, readyDelayTicks{0}, widthGrowTicks{0}, beamTravelTicks{0}, totalFiringTicks{0};
	std::uint64_t launchFxTicks{30}, doubleClickTicks{500};
	Engine::Math::Fixed swathDistance, swathAmplitude, scorchScalar{Engine::Math::Fixed::One()}, damagePerSecond,
		damageRadiusScalar{Engine::Math::Fixed::One()}, drivingSpeed, fastDrivingSpeed;
	std::uint32_t totalScorchMarks{0}, totalPulses{0};
	std::string damageType{"LASER"}, deathType{"LASERED"};
	std::string groundHitFX, launchFX, beamObject, remnant;
};

std::optional<ParticleCannonModule> ReadParticleCannon(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "ParticleUplinkCannonUpdate")
			continue;
		const auto ticks = [&](std::string_view key, std::uint64_t fallback) -> std::uint64_t {
			if (module.block->Find(key) == nullptr)
				return fallback;
			const std::int64_t milliseconds = detail::Fixed(module.block->Find(key)).Ceil();
			return milliseconds <= 0 ? 0 : static_cast<std::uint64_t>((milliseconds * static_cast<std::int64_t>(step.TicksPerSecond()) + 999) / 1000);
		};
		const auto real = [&](std::string_view key, Engine::Math::Fixed fallback) {
			return module.block->Find(key) != nullptr ? detail::Fixed(module.block->Find(key)) : fallback;
		};
		const auto name = [&](std::string_view key, std::string fallback = {}) {
			const auto *node = module.block->Find(key);
			return node != nullptr && !node->values.empty() ? std::string(node->Value()) : fallback;
		};
		const auto whole = [&](std::string_view key) {
			const auto *node = module.block->Find(key);
			return node != nullptr ? static_cast<std::uint32_t>(std::max<std::int64_t>(0, engine::config::values::ParseInt(node->Value()).value_or(0))) : 0u;
		};
		ParticleCannonModule data;
		data.power = name("SpecialPowerTemplate");
		data.beginChargeTicks = ticks("BeginChargeTime", 0);
		data.raiseAntennaTicks = ticks("RaiseAntennaTime", 0);
		data.readyDelayTicks = ticks("ReadyDelayTime", 0);
		data.widthGrowTicks = ticks("WidthGrowTime", 0);
		data.beamTravelTicks = ticks("BeamTravelTime", 0);
		data.totalFiringTicks = ticks("TotalFiringTime", 0);
		data.launchFxTicks = ticks("DelayBetweenLaunchFX", 30);
		data.doubleClickTicks = ticks("DoubleClickToFastDriveDelay", 500);
		data.swathDistance = real("SwathOfDeathDistance", {});
		data.swathAmplitude = real("SwathOfDeathAmplitude", {});
		data.scorchScalar = real("ScorchMarkScalar", Engine::Math::Fixed::One());
		data.damagePerSecond = real("DamagePerSecond", {});
		data.damageRadiusScalar = real("DamageRadiusScalar", Engine::Math::Fixed::One());
		data.drivingSpeed = real("ManualDrivingSpeed", {});
		data.fastDrivingSpeed = real("ManualFastDrivingSpeed", {});
		data.totalScorchMarks = whole("TotalScorchMarks");
		data.totalPulses = whole("TotalDamagePulses");
		data.damageType = name("DamageType", "LASER");
		data.deathType = name("DeathType", "LASERED");
		data.groundHitFX = name("GroundHitFX");
		data.launchFX = name("BeamLaunchFX");
		data.beamObject = name("ParticleBeamLaserName");
		data.remnant = name("DamagePulseRemnantObjectName");
		return data;
	}
	return std::nullopt;
}

// MissileLauncherBuildingUpdate: the power whose readiness drives its door (SpecialPowerTemplate), the door's times
// (DoorOpenTime, DoorWaitOpenTime, DoorCloseTime: ms up to whole ticks), the effects of each state (DoorOpeningFX,
// DoorOpenFX, DoorWaitingToCloseFX, DoorClosingFX, DoorClosedFX) and the sound looping while it is open
// (DoorOpenIdleAudio).
struct MissileLauncherModule
{
	std::string power;
	std::uint64_t openTicks{0};
	std::uint64_t waitOpenTicks{0};
	std::uint64_t closeTicks{0};
	std::string openingFX, openFX, waitingToCloseFX, closingFX, closedFX;
	std::string openIdleAudio;
};

std::optional<MissileLauncherModule> ReadMissileLauncher(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "MissileLauncherBuildingUpdate")
			continue;
		const auto ticks = [&](std::string_view key) -> std::uint64_t {
			const std::int64_t milliseconds = detail::Fixed(module.block->Find(key)).Ceil();
			return milliseconds <= 0 ? 0 : static_cast<std::uint64_t>((milliseconds * static_cast<std::int64_t>(step.TicksPerSecond()) + 999) / 1000);
		};
		const auto name = [&](std::string_view key) {
			const auto *node = module.block->Find(key);
			return node != nullptr && !node->values.empty() ? std::string(node->Value()) : std::string{};
		};
		MissileLauncherModule data;
		data.power = name("SpecialPowerTemplate");
		data.openTicks = ticks("DoorOpenTime");
		data.waitOpenTicks = ticks("DoorWaitOpenTime");
		data.closeTicks = ticks("DoorCloseTime");
		data.openingFX = name("DoorOpeningFX");
		data.openFX = name("DoorOpenFX");
		data.waitingToCloseFX = name("DoorWaitingToCloseFX");
		data.closingFX = name("DoorClosingFX");
		data.closedFX = name("DoorClosedFX");
		data.openIdleAudio = name("DoorOpenIdleAudio");
		return data;
	}
	return std::nullopt;
}

std::optional<CountermeasuresModule> ReadCountermeasures(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "CountermeasuresBehavior")
			continue;
		const auto ticks = [&](std::string_view key) -> std::uint64_t {
			const std::int64_t milliseconds = detail::Fixed(module.block->Find(key)).Ceil();
			return milliseconds <= 0 ? 0 : static_cast<std::uint64_t>((milliseconds * static_cast<std::int64_t>(step.TicksPerSecond()) + 999) / 1000);
		};
		const auto whole = [&](std::string_view key) -> std::uint32_t {
			const auto *node = module.block->Find(key);
			return node != nullptr ? static_cast<std::uint32_t>(std::max<std::int64_t>(0, engine::config::values::ParseInt(node->Value()).value_or(0))) : 0u;
		};
		CountermeasuresModule data;
		if (const auto *flare = module.block->Find("FlareTemplateName"); flare != nullptr && !flare->values.empty())
			data.flare = std::string(flare->Value());
		data.volleySize = whole("VolleySize");
		data.arcDegrees = detail::Fixed(module.block->Find("VolleyArcAngle"));
		data.velocityFactor = detail::Fixed(module.block->Find("VolleyVelocityFactor"));
		data.volleyTicks = ticks("DelayBetweenVolleys");
		data.volleys = whole("NumberOfVolleys");
		data.reloadTicks = ticks("ReloadTime");
		// parsePercentToReal: "50%" or 50 -> 0.5.
		if (const auto *rate = module.block->Find("EvasionRate"))
		{
			std::string_view text = rate->Value();
			if (!text.empty() && text.back() == '%')
				text.remove_suffix(1);
			data.evasionRate = engine::config::values::ParseFixed(text).value_or(Engine::Math::Fixed{}) / Engine::Math::Fixed::FromInt(100);
		}
		data.mustReloadAtAirfield = detail::Yes(module.block->Find("MustReloadAtAirfield"));
		data.decoyTicks = ticks("MissileDecoyDelay");
		data.reactionTicks = ticks("ReactionLaunchLatency");
		data.startsActive = detail::Yes(module.block->Find("StartsActive"));
		return data;
	}
	return std::nullopt;
}

// The object's SpyVisionUpdate modules, in order.
std::vector<SpyVisionModule> ReadSpyVisions(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	std::vector<SpyVisionModule> out;
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "SpyVisionUpdate")
			continue;
		const auto ticks = [&](std::string_view key) -> std::uint64_t {
			const std::int64_t milliseconds = detail::Fixed(module.block->Find(key)).Ceil();
			return milliseconds <= 0 ? 0 : static_cast<std::uint64_t>((milliseconds * static_cast<std::int64_t>(step.TicksPerSecond()) + 999) / 1000);
		};
		SpyVisionModule data;
		data.needsUpgrade = detail::Yes(module.block->Find("NeedsUpgrade"));
		data.selfPowered = detail::Yes(module.block->Find("SelfPowered"));
		data.durationTicks = ticks("SelfPoweredDuration");
		data.intervalTicks = ticks("SelfPoweredInterval");
		if (const auto *kinds = module.block->Find("SpyOnKindof"))
			for (const std::string_view kind : kinds->values)
				data.kinds.emplace_back(kind);
		out.push_back(std::move(data));
	}
	return out;
}

std::optional<CleanupHazardModule> ReadCleanupHazard(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "CleanupHazardUpdate")
			continue;
		CleanupHazardModule data;
		const std::int64_t milliseconds = detail::Fixed(module.block->Find("ScanRate")).Ceil();
		data.scanTicks = milliseconds <= 0 ? 0 : static_cast<std::uint64_t>((milliseconds * static_cast<std::int64_t>(step.TicksPerSecond()) + 999) / 1000);
		data.scanRange = detail::Fixed(module.block->Find("ScanRange"));
		return data;
	}
	return std::nullopt;
}

// A CashHackSpecialPower module: what it steals (MoneyAmount), unless its player knows the science of an
// UpgradeMoneyAmount line (the first such, in order: findAmountToSteal).
struct CashHackModule
{
	std::int64_t amount{0};
	std::vector<std::pair<std::string, std::int64_t>> upgrades;
};

std::optional<CashHackModule> FindCashHack(const ObjectDefinition &object, std::string_view specialPower)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "CashHackSpecialPower")
			continue;
		const auto *power = module.block->Find("SpecialPowerTemplate");
		if (power == nullptr || !detail::Same(power->Value(), specialPower))
			continue;
		CashHackModule found;
		for (const engine::config::Node &field : module.block->children)
			if (field.key == "MoneyAmount" && !field.values.empty())
				found.amount = engine::config::values::ParseInt(field.Value()).value_or(0);
			else if (field.key == "UpgradeMoneyAmount" && field.values.size() >= 2)
				found.upgrades.emplace_back(std::string(field.values[0]), engine::config::values::ParseInt(field.values[1]).value_or(0));
		return found;
	}
	return std::nullopt;
}

// A CashBountyPower module: its power and its Bounty (parsePercentToReal) in hundredths of a percent.
struct CashBountyModule
{
	std::string power;
	std::int64_t share{0};
};

std::vector<CashBountyModule> CashBountyModulesOf(const ObjectDefinition &object)
{
	std::vector<CashBountyModule> out;
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "CashBountyPower")
			continue;
		const auto *power = module.block->Find("SpecialPowerTemplate");
		if (power == nullptr || power->values.empty())
			continue;
		CashBountyModule found{std::string(power->Value())};
		if (const auto *bounty = module.block->Find("Bounty"); bounty != nullptr && !bounty->values.empty())
		{
			std::string_view text = bounty->Value();
			if (!text.empty() && text.back() == '%')
				text.remove_suffix(1);
			const Engine::Math::Fixed percent = engine::config::values::ParseFixed(text).value_or(Engine::Math::Fixed{});
			found.share = (percent * Engine::Math::Fixed::FromInt(100)).Round();
		}
		out.push_back(std::move(found));
	}
	return out;
}

// Whether it has a SpecialPowerCreate module (its powers start their countdown as it is built).
bool HasSpecialPowerCreate(const ObjectDefinition &object)
{
	return std::any_of(object.modules.begin(), object.modules.end(), [](const ModuleEntry &module) { return module.type == "SpecialPowerCreate"; });
}

// An object's DeliverPayloadAIUpdate module data (DeliverPayloadAIUpdateModuleData), what deliverPayloadViaModuleData
// runs a scripted reinforcement's delivery with: DoorDelay, PutInContainer, DeliveryDistance, MaxAttempts (none given:
// 0), DropDelay, DropOffset and DropVariance (durations in milliseconds, rounded up to ticks). None without the module.
struct PayloadModule
{
	std::string putInContainer;
	Engine::Math::Fixed deliveryDistance;
	std::int32_t maxAttempts{0};
	std::uint32_t reserved{0};
	std::uint64_t doorDelay{0};
	std::uint64_t dropDelay{0};
	Engine::Math::FixedVector3 dropOffset;
	Engine::Math::FixedVector3 dropVariance;
};

std::optional<PayloadModule> ReadDeliverPayloadModule(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	const auto ticks = [&](const engine::config::Node &field) -> std::uint64_t {
		const std::int64_t milliseconds = detail::Fixed(&field).Ceil();
		return milliseconds <= 0 ? 0 : static_cast<std::uint64_t>((milliseconds * static_cast<std::int64_t>(step.TicksPerSecond()) + 999) / 1000);
	};
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "DeliverPayloadAIUpdate")
			continue;
		PayloadModule data;
		for (const engine::config::Node &field : module.block->children)
		{
			if (field.key == "DoorDelay")
				data.doorDelay = ticks(field);
			else if (field.key == "PutInContainer")
				data.putInContainer = std::string(field.Value());
			else if (field.key == "DeliveryDistance")
				data.deliveryDistance = detail::Fixed(&field);
			else if (field.key == "MaxAttempts")
				data.maxAttempts = static_cast<std::int32_t>(engine::config::values::ParseInt(field.Value()).value_or(0));
			else if (field.key == "DropDelay")
				data.dropDelay = ticks(field);
			else if (field.key == "DropOffset")
				data.dropOffset = detail::Coord(field);
			else if (field.key == "DropVariance")
				data.dropVariance = detail::Coord(field);
		}
		return data;
	}
	return std::nullopt;
}

// The creation list an object runs for a special power (its OCLSpecialPower
// module naming that power), if it has one.
std::optional<OclPower> FindOclPower(const ObjectDefinition &object, std::string_view specialPower)
{
	constexpr std::pair<std::string_view, CreateLocation> locations[] = {{"CREATE_AT_EDGE_NEAR_SOURCE", CreateLocation::AtEdgeNearSource},
		{"CREATE_AT_EDGE_NEAR_TARGET", CreateLocation::AtEdgeNearTarget}, {"CREATE_AT_LOCATION", CreateLocation::AtLocation},
		{"USE_OWNER_OBJECT", CreateLocation::UseOwnerObject}, {"CREATE_ABOVE_LOCATION", CreateLocation::AboveLocation},
		{"CREATE_AT_EDGE_FARTHEST_FROM_TARGET", CreateLocation::AtEdgeFarthestFromTarget}};
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "OCLSpecialPower")
			continue;
		const auto *power = module.block->Find("SpecialPowerTemplate");
		const auto *ocl = module.block->Find("OCL");
		if (power == nullptr || ocl == nullptr || !detail::Same(power->Value(), specialPower))
			continue;
		OclPower found{std::string(ocl->Value()), CreateLocation::AtEdgeNearSource};
		if (const auto *where = module.block->Find("CreateLocation"))
			for (const auto &[name, location] : locations)
				if (detail::Same(where->Value(), name))
					found.location = location;
		return found;
	}
	return std::nullopt;
}
}
