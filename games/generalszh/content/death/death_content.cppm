export module games.generalszh.content.death.death_content;
import std;

export import engine.gameplay.rts.death.definitions.death_definition;
export import games.generalszh.content.objects.object_definition;
export import games.generalszh.content.combat.combat_catalog;
export import games.generalszh.content.upgrades.upgrade_content;
import games.generalszh.content.objects.object_status;
import Engine.Core.Math.FixedAngle;
import engine.gameplay.rts.death.algorithms.structure_topple;
import engine.config.binding.values;

// How a Zero Hour object dies, from its die modules, bound onto the engine's
// death definition:
// - DestroyDie: removed at once;
// - FXListDie (DeathFX), CreateObjectDie (CreationList) and
//   InstantDeathBehavior (FX / OCL / Weapon lists): effects at the moment
//   of death;
// - the SlowDeathBehavior family: the weighted slow deaths, with their sink
//   and destruction timing and FX / OCL / Weapon lists per phase.
// Every one filters by DeathTypes and VeterancyLevels ("ALL", "NONE",
// "+NAME", "-NAME"). KeepObjectDie and bodies nothing removes linger.
// Effect names become ids through `intern` (the game's tables resolve them;
// names the original could not find still take their turn in the random
// pick and play nothing, as there).
export namespace generalszh::content
{
inline constexpr std::array<std::string_view, 4> VeterancyNames{"REGULAR", "VETERAN", "ELITE", "HEROIC"};

using DeathEffectIntern = std::function<std::uint32_t(engine::gameplay::DeathEffectKind, std::string_view)>;

namespace death_detail
{
using engine::config::Node;
using engine::gameplay::DeathEffectKind;
using engine::gameplay::DeathPhase;

bool Same(std::string_view a, std::string_view b)
{
	return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
		return std::toupper(static_cast<unsigned char>(x)) == std::toupper(static_cast<unsigned char>(y));
	});
}

template<std::size_t Count>
std::optional<std::uint32_t> IndexOf(const std::array<std::string_view, Count> &names, std::string_view name)
{
	for (std::uint32_t index = 0; index < Count; ++index)
		if (Same(names[index], name))
			return index;
	return std::nullopt;
}

// The original's flag lists: start from all; ALL, NONE, +NAME, -NAME.
template<std::size_t Count>
std::uint32_t Flags(const Node *node, const std::array<std::string_view, Count> &names, std::uint32_t all)
{
	std::uint32_t flags = all;
	if (node == nullptr)
		return flags;
	for (const std::string_view token : node->values)
	{
		if (Same(token, "ALL"))
			flags = all;
		else if (Same(token, "NONE"))
			flags = 0;
		else if ((token.starts_with('+') || token.starts_with('-')) && token.size() > 1)
		{
			if (const auto index = IndexOf(names, token.substr(1)))
				flags = token.front() == '+' ? flags | (1u << *index) : flags & ~(1u << *index);
		}
	}
	return flags;
}

engine::gameplay::DeathFilter Filter(const Node &block)
{
	engine::gameplay::DeathFilter filter{Flags(block.Find("DeathTypes"), DeathTypeNames, engine::gameplay::AllDeathTypes),
		static_cast<std::uint8_t>(Flags(block.Find("VeterancyLevels"), VeterancyNames, engine::gameplay::AllVeterancyLevels))};
	// DieMuxData's ExemptStatus and RequiredStatus (ObjectStatusMaskType names).
	if (const Node *exempt = block.Find("ExemptStatus"))
		filter.exemptStatus = ObjectStatusMask(exempt->values);
	if (const Node *required = block.Find("RequiredStatus"))
		filter.requiredStatus = ObjectStatusMask(required->values);
	return filter;
}

// Upgrade-triggered die modules start inactive (upgrades are not ported yet).
bool Active(const Node &block) { return block.Find("TriggeredBy") == nullptr; }

std::optional<DeathEffectKind> ListKind(std::string_view key)
{
	if (Same(key, "FX"))
		return DeathEffectKind::Effect;
	if (Same(key, "OCL"))
		return DeathEffectKind::Objects;
	if (Same(key, "Weapon"))
		return DeathEffectKind::Weapon;
	return std::nullopt;
}

std::optional<DeathPhase> PhaseOf(std::string_view name)
{
	if (Same(name, "INITIAL"))
		return DeathPhase::Initial;
	if (Same(name, "MIDPOINT"))
		return DeathPhase::Midpoint;
	if (Same(name, "FINAL"))
		return DeathPhase::Final;
	return std::nullopt;
}
}

namespace death_detail
{
// NeutronMissileSlowDeathBehaviorModuleData: a field of its blast waves (ScorchMarkSize, FXList, BlastNEnabled /
// Delay / ScorchDelay / InnerRadius / OuterRadius / MaxDamage / MinDamage / ToppleSpeed, N 1 to 9; PushForce is read
// and unused, its push commented out in doBlast). Delays are parseDurationReal milliseconds, as frames; a blast goes
// off on the first frame more than that after (currFrame - m_activationFrame > delay). Its damage is EXPLOSION and
// its deaths EXPLODED (doBlast).
inline constexpr std::size_t MaxBlasts = 9;
using Blasts = std::array<std::pair<bool, engine::gameplay::BlastDefinition>, MaxBlasts>;

bool BlastField(const engine::config::Node &child, engine::gameplay::BlastWaveDefinition &wave, Blasts &blasts, engine::config::BindContext &bind,
	const DeathEffectIntern &intern)
{
	using Engine::Math::Fixed;
	const std::string_view key = child.key;
	const auto fixed = [&] { return engine::config::values::ParseFixed(child.Value()).value_or(Fixed{}); };
	if (Same(key, "ScorchMarkSize"))
	{
		wave.scorchSize = fixed();
		return true;
	}
	if (Same(key, "FXList"))
	{
		if (!child.Value().empty() && !Same(child.Value(), "None"))
			wave.effect = intern(engine::gameplay::DeathEffectKind::Effect, child.Value());
		return true;
	}
	if (key.size() < 7 || !Same(key.substr(0, 5), "Blast") || key[5] < '1' || key[5] > '9')
		return false;
	auto &[enabled, blast] = blasts[static_cast<std::size_t>(key[5] - '1')];
	const std::string_view field = key.substr(6);
	const auto after = [&] {
		const Fixed frames = fixed() * Fixed::FromInt(bind.step.TicksPerSecond()) / Fixed::FromInt(1000);
		return static_cast<std::uint64_t>(std::max<std::int64_t>(frames.Floor(), -1) + 1);
	};
	if (Same(field, "Enabled"))
		enabled = engine::config::values::ParseBool(child.Value()).value_or(false);
	else if (Same(field, "Delay"))
		blast.blastAfter = after();
	else if (Same(field, "ScorchDelay"))
		blast.scorchAfter = after();
	else if (Same(field, "InnerRadius"))
		blast.innerRadius = fixed();
	else if (Same(field, "OuterRadius"))
		blast.outerRadius = fixed();
	else if (Same(field, "MaxDamage"))
		blast.maxDamage = fixed();
	else if (Same(field, "MinDamage"))
		blast.minDamage = fixed();
	else if (Same(field, "ToppleSpeed"))
		blast.toppleSpeed = fixed();
	else if (!Same(field, "PushForce"))
		return false;
	return true;
}

// A JetSlowDeathBehavior or HelicopterSlowDeathBehavior field: its crash
// (a jet's rates per frame, as the original reads them; a frame is a tick)
// and the effects of the crash's phases.
bool CrashField(const engine::config::Node &child, engine::gameplay::SlowDeathDefinition &slow, engine::config::BindContext &bind, const DeathEffectIntern &intern)
{
	using engine::gameplay::DeathEffectKind;
	using engine::gameplay::DeathPhase;
	const std::string_view key = child.key;
	auto &crash = slow.crash;
	constexpr std::pair<std::string_view, DeathPhase> phases[] = {{"OnGroundDeath", DeathPhase::OnGround}, {"InitialDeath", DeathPhase::Initial},
		{"Secondary", DeathPhase::Secondary}, {"HitGround", DeathPhase::HitGround}, {"FinalBlowUp", DeathPhase::FinalBlowUp}};
	if (crash.kind == engine::gameplay::CrashKind::Jet)
		for (const auto &[suffix, phase] : phases)
		for (const auto &[prefix, kind] : {std::pair{std::string_view{"FX"}, DeathEffectKind::Effect}, std::pair{std::string_view{"OCL"}, DeathEffectKind::Objects}})
			if (key.size() == prefix.size() + suffix.size() && Same(key.substr(0, prefix.size()), prefix) && Same(key.substr(prefix.size()), suffix))
			{
				for (const std::string_view name : child.values)
					slow.effects.Of(phase, kind).push_back(intern(kind, name));
				return true;
			}
	const auto fixed = [&] { return engine::config::values::ParseFixed(child.Value()).value_or(Engine::Math::Fixed{}); };
	const auto rate = [&] { return static_cast<std::int32_t>(Engine::Math::TurnFromRadians(fixed()).units); };
	// Degrees per second, as turn units per tick.
	const auto spin = [&] {
		return static_cast<std::int32_t>(Engine::Math::TurnFromDegrees(engine::config::ReadPerSecond(child, bind).value_or(Engine::Math::Fixed{})).units);
	};
	const auto ticks = [&] { return engine::config::ReadDurationTicks(child, bind).value_or(0); };
	if (crash.kind == engine::gameplay::CrashKind::Helicopter)
	{
		if (Same(key, "FXBlade") || Same(key, "OCLBlade"))
		{
			const auto kind = Same(key, "FXBlade") ? DeathEffectKind::Effect : DeathEffectKind::Objects;
			for (const std::string_view name : child.values)
				slow.effects.Of(DeathPhase::Blade, kind).push_back(intern(kind, name));
		}
		else if (Same(key, "OCLEjectPilot"))
		{
			if (!child.Value().empty() && !Same(child.Value(), "None"))
				crash.ejectPilot = intern(DeathEffectKind::Objects, child.Value());
		}
		else if (Same(key, "FinalRubbleObject"))
			slow.effects.Of(DeathPhase::FinalBlowUp, DeathEffectKind::Spawn).push_back(intern(DeathEffectKind::Spawn, child.Value()));
		else if (Same(key, "SpiralOrbitTurnRate"))
			crash.spiralTurnRate = spin();
		else if (Same(key, "SpiralOrbitForwardSpeed"))
			crash.spiralSpeed = engine::config::ReadPerSecond(child, bind).value_or(Engine::Math::Fixed{});
		else if (Same(key, "SpiralOrbitForwardSpeedDamping"))
			crash.spiralDamping = fixed();
		else if (Same(key, "MinSelfSpin"))
			crash.minSelfSpin = spin();
		else if (Same(key, "MaxSelfSpin"))
			crash.maxSelfSpin = spin();
		else if (Same(key, "SelfSpinUpdateDelay"))
			crash.spinDelay = ticks();
		else if (Same(key, "SelfSpinUpdateAmount"))
			// Degrees, spread over a second's ticks as the original.
			crash.spinStep = static_cast<std::int32_t>(Engine::Math::TurnFromDegrees(fixed() / Engine::Math::Fixed::FromInt(bind.step.TicksPerSecond())).units);
		else if (Same(key, "FallHowFast"))
			crash.fallFactor = engine::config::ReadPercent(child, bind).value_or(Engine::Math::Fixed{});
		else if (Same(key, "MaxBraking"))
			crash.maxBraking = engine::config::ReadPerSecondSquared(child, bind).value_or(crash.maxBraking);
		else if (Same(key, "MinBladeFlyOffDelay"))
			crash.bladeDelayMin = ticks();
		else if (Same(key, "MaxBladeFlyOffDelay"))
			crash.bladeDelayMax = ticks();
		else if (Same(key, "DelayFromGroundToFinalDeath"))
			crash.finalDelay = ticks();
		else if (Same(key, "FXHitGround") || Same(key, "OCLHitGround") || Same(key, "FXFinalBlowUp") || Same(key, "OCLFinalBlowUp"))
		{
			const auto kind = key.starts_with("FX") ? DeathEffectKind::Effect : DeathEffectKind::Objects;
			const auto phase = key.ends_with("HitGround") ? DeathPhase::HitGround : DeathPhase::FinalBlowUp;
			for (const std::string_view name : child.values)
				slow.effects.Of(phase, kind).push_back(intern(kind, name));
		}
		else
			return false;
		return true;
	}
	if (Same(key, "RollRate"))
		crash.rollRate = rate();
	else if (Same(key, "PitchRate"))
		crash.pitchRate = rate();
	else if (Same(key, "RollRateDelta"))
		crash.rollRateDelta = engine::config::ReadPercent(child, bind).value_or(Engine::Math::Fixed::One());
	else if (Same(key, "FallHowFast"))
		crash.fallFactor = engine::config::ReadPercent(child, bind).value_or(Engine::Math::Fixed{});
	else if (Same(key, "DelaySecondaryFromInitialDeath"))
		crash.secondaryDelay = engine::config::ReadDurationTicks(child, bind).value_or(0);
	else if (Same(key, "DelayFinalBlowUpFromHitGround"))
		crash.finalDelay = engine::config::ReadDurationTicks(child, bind).value_or(0);
	else
		return false;
	return true;
}
}

engine::gameplay::DeathDefinition ReadObjectDeath(const ObjectDefinition &object, const engine::time::FixedStep &step, const DeathEffectIntern &intern,
	const UpgradeCatalog *upgrades = nullptr)
{
	using namespace death_detail;
	engine::gameplay::DeathDefinition death;
	death.majorRadius = object.geometry.majorRadius;
	death.hulk = object.Is("HULK");
	engine::config::Diagnostics diagnostics;
	engine::config::BindContext bind{diagnostics, step};
	// UpgradeMux: StartsActive, TriggeredBy, ConflictsWith, RequiresAllTriggers.
	const auto gateOf = [&](const Node &block) {
		engine::gameplay::DieGate gate;
		gate.upgradeSwitched = true;
		if (const Node *starts = block.Find("StartsActive"))
			gate.startsActive = engine::config::values::ParseBool(starts->Value()).value_or(false);
		const auto mask = [&](const Node *node) {
			engine::gameplay::UpgradeMask bits;
			if (node != nullptr && upgrades != nullptr)
				for (const std::string_view name : node->values)
					if (const auto bit = upgrades->Find(name))
						bits.Set(*bit);
			return bits;
		};
		gate.activation = mask(block.Find("TriggeredBy"));
		gate.conflicting = mask(block.Find("ConflictsWith"));
		if (const Node *all = block.Find("RequiresAllTriggers"))
			gate.requiresAll = engine::config::values::ParseBool(all->Value()).value_or(false);
		return gate;
	};
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr)
			continue;
		const Node &block = *module.block;
		const std::string_view type = module.type;
		// FireWeaponWhenDeadBehavior (an UpgradeMux): its DeathWeapon, switched on from the start or by upgrades.
		if (type == "FireWeaponWhenDeadBehavior")
		{
			const Node *weapon = block.Find("DeathWeapon");
			if (weapon == nullptr || weapon->Value().empty() || Same(weapon->Value(), "None"))
				continue;
			engine::gameplay::DieEffect effect{Filter(block), DeathEffectKind::Weapon, {intern(DeathEffectKind::Weapon, weapon->Value())}, {}};
			effect.gate = gateOf(block);
			death.atDeath.push_back(std::move(effect));
			continue;
		}
		// FXListDie is an UpgradeMux too: with TriggeredBy it plays only once upgraded (isUpgradeActive); without, it
		// is switched on as the object is made; either way not with a conflicting upgrade (object's or player's).
		if (type == "FXListDie")
		{
			if (const Node *fx = block.Find("DeathFX"); fx != nullptr && !fx->Value().empty() && !Same(fx->Value(), "None"))
			{
				engine::gameplay::DieEffect effect{Filter(block), DeathEffectKind::Effect, {intern(DeathEffectKind::Effect, fx->Value())}};
				if (const Node *orient = block.Find("OrientToObject"))
					effect.orient = engine::config::values::ParseBool(orient->Value()).value_or(true);
				effect.gate = gateOf(block);
				if (block.Find("TriggeredBy") == nullptr)
					effect.gate.startsActive = true;
				death.atDeath.push_back(std::move(effect));
			}
			continue;
		}
		if (!Active(block))
			continue;
		// EjectPilotDie::ejectPilot: its air or ground creation list, with the unit's VoiceEject and SoundEject.
		if (type == "EjectPilotDie")
		{
			for (const auto &[key, altitude] : {std::pair{std::string_view{"GroundCreationList"}, engine::gameplay::DieAltitude::Ground},
					 std::pair{std::string_view{"AirCreationList"}, engine::gameplay::DieAltitude::Air}})
			{
				const Node *list = block.Find(key);
				if (list == nullptr || list->Value().empty() || Same(list->Value(), "None"))
					continue;
				engine::gameplay::DieEffect effect{Filter(block), DeathEffectKind::Objects, {intern(DeathEffectKind::Objects, list->Value())}, {}, altitude};
				death.atDeath.push_back(effect);
				for (const std::string_view role : {std::string_view{"VoiceEject"}, std::string_view{"SoundEject"}})
					if (const std::string_view sound = object.Sound(role); !sound.empty())
						death.atDeath.push_back({Filter(block), DeathEffectKind::Sound, {intern(DeathEffectKind::Sound, sound)}, {}, altitude});
			}
			continue;
		}
		// CreateCrateDie: each of its CrateData is tried on its own (CreateCrateDie::onDie).
		if (type == "CreateCrateDie")
		{
			for (const Node &child : block.children)
				if (Same(child.key, "CrateData") && !child.Value().empty())
					death.atDeath.push_back({Filter(block), DeathEffectKind::Loot, {intern(DeathEffectKind::Loot, child.Value())}});
			continue;
		}
		if (type == "CrushDie")
		{
			engine::gameplay::CrushDieDefinition crush;
			crush.filter = Filter(block);
			constexpr std::array<std::string_view, 3> prefixes{"TotalCrush", "BackEndCrush", "FrontEndCrush"};
			for (std::size_t at = 0; at < prefixes.size(); ++at)
			{
				const std::string sound = std::string(prefixes[at]) + "Sound";
				const std::string percent = std::string(prefixes[at]) + "SoundPercent";
				if (const Node *node = block.Find(sound); node != nullptr && !node->Value().empty() && !Same(node->Value(), "NoSound"))
					crush.sound[at] = intern(DeathEffectKind::Sound, node->Value());
				if (const Node *node = block.Find(percent))
					crush.percent[at] = static_cast<std::uint8_t>(std::clamp<std::int64_t>(engine::config::ReadInt(*node, bind).value_or(0), 0, 100));
			}
			death.crushed.push_back(crush);
			continue;
		}
		if (type == "DestroyDie")
			death.destroyedAtOnce.push_back(Filter(block));
		else if (type == "StructureCollapseUpdate")
		{
			engine::gameplay::CollapseDefinition collapse;
			collapse.filter = Filter(block);
			// Geometry::getMaxHeightAbovePosition: a sphere's radius, else its height.
			collapse.height = object.geometry.shape == GeometryShape::Sphere ? object.geometry.majorRadius : object.geometry.height;
			for (const Node &child : block.children)
			{
				const std::string_view key = child.key;
				const auto ticks = [&] { return engine::config::ReadDurationTicks(child, bind).value_or(0); };
				if (Same(key, "MinCollapseDelay"))
					collapse.minCollapseDelay = ticks();
				else if (Same(key, "MaxCollapseDelay"))
					collapse.maxCollapseDelay = ticks();
				else if (Same(key, "MinBurstDelay"))
					collapse.minBurstDelay = ticks();
				else if (Same(key, "MaxBurstDelay"))
					collapse.maxBurstDelay = ticks();
				else if (Same(key, "BigBurstFrequency"))
					collapse.bigBurstFrequency = static_cast<std::uint32_t>(std::max<std::int64_t>(0, engine::config::ReadInt(child, bind).value_or(0)));
				else if (Same(key, "CollapseDamping"))
					collapse.damping = engine::config::ReadFixed(child, bind).value_or(collapse.damping);
				else if (Same(key, "MaxShudder"))
					collapse.maxShudder = engine::config::ReadFixed(child, bind).value_or(collapse.maxShudder);
				else if (Same(key, "OCL") || Same(key, "FXList"))
				{
					// "OCL = INITIAL OCL_A OCL_B": candidates for that phase (INITIAL, DELAY, BURST, FINAL).
					constexpr std::array<std::string_view, 4> phases{"INITIAL", "DELAY", "BURST", "FINAL"};
					const auto phase = IndexOf(phases, child.Value(0));
					if (!phase)
						continue;
					const DeathEffectKind kind = Same(key, "OCL") ? DeathEffectKind::Objects : DeathEffectKind::Effect;
					auto &list = kind == DeathEffectKind::Objects ? collapse.objects[*phase] : collapse.effects[*phase];
					for (std::size_t index = 1; index < child.values.size(); ++index)
						list.push_back(intern(kind, child.values[index]));
				}
			}
			death.collapses.push_back(std::move(collapse));
		}
		else if (type == "StructureToppleUpdate")
		{
			namespace topple = engine::gameplay::structure_topple;
			engine::gameplay::StructureToppleDefinition how;
			how.filter = Filter(block);
			// Geometry::getMaxHeightAbovePosition (taken as it stands: its rubble state is barely tall).
			how.height = object.geometry.shape == GeometryShape::Sphere ? object.geometry.majorRadius : object.geometry.height;
			how.majorRadius = object.geometry.majorRadius;
			how.minorRadius = object.geometry.minorRadius;
			const auto effect = [&](const Node &child) {
				return child.Value().empty() || Same(child.Value(), "None") ? engine::gameplay::StructureToppleDefinition::None :
					intern(DeathEffectKind::Effect, child.Value());
			};
			for (const Node &child : block.children)
			{
				const std::string_view key = child.key;
				const auto ticks = [&] { return engine::config::ReadDurationTicks(child, bind).value_or(0); };
				if (Same(key, "MinToppleDelay"))
					how.minToppleDelay = ticks();
				else if (Same(key, "MaxToppleDelay"))
					how.maxToppleDelay = ticks();
				else if (Same(key, "MinToppleBurstDelay"))
					how.minBurstDelay = ticks();
				else if (Same(key, "MaxToppleBurstDelay"))
					how.maxBurstDelay = ticks();
				else if (Same(key, "StructuralIntegrity"))
					how.integrity = topple::FromFixed(engine::config::ReadFixed(child, bind).value_or(topple::ToFixed(how.integrity)));
				else if (Same(key, "StructuralDecay"))
					how.decay = topple::FromFixed(engine::config::ReadFixed(child, bind).value_or(Engine::Math::Fixed{}));
				else if (Same(key, "DamageFXTypes"))
					how.damageFxTypes = ParseDamageTypeFlags(child, how.damageFxTypes);
				else if (Same(key, "ToppleStartFX"))
					how.startEffect = effect(child);
				else if (Same(key, "ToppleDelayFX"))
					how.delayEffect = effect(child);
				else if (Same(key, "ToppleDoneFX"))
					how.doneEffect = effect(child);
				else if (Same(key, "CrushingFX"))
					how.crushingEffect = effect(child);
				else if (Same(key, "CrushingWeaponName") && !child.Value().empty())
					how.crushingWeapon = intern(DeathEffectKind::Weapon, child.Value());
				else if (Same(key, "AngleFX") && child.values.size() >= 2)
				{
					// parseAngleFX: degrees, then its FX list.
					const auto degrees = engine::config::values::ParseFixed(child.values[0]);
					if (degrees && !Same(child.values[1], "None"))
						how.angleEffects.push_back({topple::RadiansFromDegrees(*degrees), intern(DeathEffectKind::Effect, child.values[1])});
				}
				else if (Same(key, "OCL"))
				{
					// "OCL = INITIAL OCL_A OCL_B": candidates for that phase (INITIAL, DELAY, FINAL).
					constexpr std::array<std::string_view, 3> phases{"INITIAL", "DELAY", "FINAL"};
					const auto phase = IndexOf(phases, child.Value(0));
					if (!phase)
						continue;
					for (std::size_t index = 1; index < child.values.size(); ++index)
						how.objects[*phase].push_back(intern(DeathEffectKind::Objects, child.values[index]));
				}
			}
			death.topples.push_back(std::move(how));
		}
		else if (type == "RebuildHoleExposeDie")
		{
			// onDie: its hole takes its place (the game's rules: HoleMaxHealth, TransferAttackers from the module).
			if (const Node *hole = block.Find("HoleName"); hole != nullptr && !hole->Value().empty())
				death.atDeath.push_back({Filter(block), DeathEffectKind::Replace, {intern(DeathEffectKind::Replace, hole->Value())}});
		}
		else if (type == "SpecialPowerCompletionDie")
		{
			// notifyScriptEngine: the power, told of as it dies (to its creator's credit, if it has one).
			if (const Node *power = block.Find("SpecialPowerTemplate"); power != nullptr && !power->Value().empty())
				death.atDeath.push_back({Filter(block), DeathEffectKind::Notice, {intern(DeathEffectKind::Notice, power->Value())}});
		}
		else if (type == "UpgradeDie")
		{
			// onDie: its producer gives up UpgradeToRemove (a drone lost: its maker may build another).
			if (const Node *upgrade = block.Find("UpgradeToRemove"); upgrade != nullptr && !upgrade->Value().empty())
				death.atDeath.push_back({Filter(block), DeathEffectKind::Release, {intern(DeathEffectKind::Release, upgrade->Value())}});
		}
		else if (type == "CreateObjectDie")
		{
			if (const Node *list = block.Find("CreationList"); list != nullptr && !list->Value().empty() && !Same(list->Value(), "None"))
			{
				engine::gameplay::DieEffect effect{Filter(block), DeathEffectKind::Objects, {intern(DeathEffectKind::Objects, list->Value())}};
				if (const Node *transfer = block.Find("TransferPreviousHealth"))
					effect.transferHealth = engine::config::values::ParseBool(transfer->Value()).value_or(false);
				death.atDeath.push_back(std::move(effect));
			}
		}
		else if (type == "InstantDeathBehavior")
		{
			// One pick per list, all at the moment of death.
			for (const DeathEffectKind kind : {DeathEffectKind::Effect, DeathEffectKind::Objects, DeathEffectKind::Weapon})
			{
				engine::gameplay::DieEffect effect{Filter(block), kind, {}};
				for (const Node &child : block.children)
					if (ListKind(child.key) == kind)
						for (const std::string_view name : child.values)
							effect.candidates.push_back(intern(kind, name));
				if (!effect.candidates.empty())
					death.atDeath.push_back(std::move(effect));
			}
		}
		else if (type.ends_with("SlowDeathBehavior"))
		{
			engine::gameplay::SlowDeathDefinition slow;
			slow.filter = Filter(block);
			if (type == "JetSlowDeathBehavior")
				slow.crash.kind = engine::gameplay::CrashKind::Jet;
			else if (type == "HelicopterSlowDeathBehavior")
				slow.crash.kind = engine::gameplay::CrashKind::Helicopter;
			const bool neutron = type == "NeutronMissileSlowDeathBehavior";
			Blasts blasts{};
			for (const Node &child : block.children)
			{
				const std::string_view key = child.key;
				if (neutron && BlastField(child, slow.wave, blasts, bind, intern))
					continue;
				if (Same(key, "SinkRate"))
					slow.sinkRate = engine::config::ReadPerSecond(child, bind).value_or(slow.sinkRate);
				else if (Same(key, "ProbabilityModifier"))
					slow.probability = static_cast<std::uint32_t>(std::max<std::int64_t>(0, engine::config::ReadInt(child, bind).value_or(10)));
				else if (Same(key, "ModifierBonusPerOverkillPercent"))
					slow.overkillBonus = engine::config::ReadPercent(child, bind).value_or(slow.overkillBonus);
				else if (Same(key, "SinkDelay"))
					slow.sinkDelay = engine::config::ReadDurationTicks(child, bind).value_or(0);
				else if (Same(key, "SinkDelayVariance"))
					slow.sinkDelayVariance = engine::config::ReadDurationTicks(child, bind).value_or(0);
				else if (Same(key, "DestructionDelay"))
					slow.destructionDelay = engine::config::ReadDurationTicks(child, bind).value_or(0);
				else if (Same(key, "DestructionDelayVariance"))
					slow.destructionDelayVariance = engine::config::ReadDurationTicks(child, bind).value_or(0);
				else if (Same(key, "FlingForce"))
					slow.fling.force = engine::config::ReadFixed(child, bind).value_or(slow.fling.force);
				else if (Same(key, "FlingForceVariance"))
					slow.fling.forceVariance = engine::config::ReadFixed(child, bind).value_or(slow.fling.forceVariance);
				else if (Same(key, "FlingPitch"))
					slow.fling.pitch = engine::config::ReadDegrees(child, bind).value_or(slow.fling.pitch);
				else if (Same(key, "FlingPitchVariance"))
					slow.fling.pitchVariance = engine::config::ReadDegrees(child, bind).value_or(slow.fling.pitchVariance);
				else if (slow.crash.kind != engine::gameplay::CrashKind::None && CrashField(child, slow, bind, intern))
					continue;
				else if (const auto kind = ListKind(key))
				{
					// "FX = INITIAL FX_A FX_B": candidates for that phase.
					const auto phase = PhaseOf(child.Value(0));
					if (!phase)
						continue;
					for (std::size_t index = 1; index < child.values.size(); ++index)
						slow.effects.Of(*phase, *kind).push_back(intern(*kind, child.values[index]));
				}
			}
			if (neutron)
			{
				for (const auto &[enabled, blast] : blasts)
					if (enabled)
						slow.wave.blasts.push_back(blast);
				slow.wave.damageType = DamageTypeIndex("EXPLOSION").value_or(0);
				slow.wave.deathType = DeathTypeIndex("EXPLODED").value_or(0);
			}
			if (slow.crash.ejectPilot != engine::gameplay::CrashDefinition::NoEject)
				for (std::size_t index = 0; const std::string_view role : {std::string_view{"VoiceEject"}, std::string_view{"SoundEject"}})
					if (const std::string_view sound = object.Sound(role); !sound.empty())
						slow.crash.ejectSounds[index++] = intern(DeathEffectKind::Sound, sound);
					else
						++index;
			death.slow.push_back(std::move(slow));
		}
	}
	return death;
}

// The sound a crashing aircraft makes on its way down (JetSlowDeathBehavior
// DeathLoopSound, HelicopterSlowDeathBehavior SoundDeathLoop); empty: none.
std::string ReadCrashLoopSound(const ObjectDefinition &object)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr)
			continue;
		const char *key = module.type == "JetSlowDeathBehavior" ? "DeathLoopSound" : module.type == "HelicopterSlowDeathBehavior" ? "SoundDeathLoop" : nullptr;
		if (key == nullptr)
			continue;
		if (const auto *sound = module.block->Find(key); sound != nullptr && !sound->Value().empty())
			return std::string(sound->Value());
	}
	return {};
}

// The particle system a crashing helicopter trails (HelicopterSlowDeathBehavior
// AttachParticle, at AttachParticleBone; without a bone at AttachParticleLoc, "X:0 Y:0 Z:0" by default: its origin).
struct CrashTrail
{
	std::string system;
	std::string bone;
	Engine::Math::FixedVector3 offset{};
};

std::optional<CrashTrail> ReadCrashTrail(const ObjectDefinition &object)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || module.type != "HelicopterSlowDeathBehavior")
			continue;
		const auto *system = module.block->Find("AttachParticle");
		if (system == nullptr || system->Value().empty())
			return std::nullopt;
		CrashTrail trail{std::string(system->Value()), {}};
		if (const auto *bone = module.block->Find("AttachParticleBone"))
			trail.bone = std::string(bone->Value());
		// INI::parseCoord3D: "X:1 Y:2 Z:3".
		if (const auto *at = module.block->Find("AttachParticleLoc"))
			for (const std::string_view token : at->values)
				if (token.size() > 2 && token[1] == ':')
					if (const auto value = engine::config::values::ParseFixed(token.substr(2)))
					{
						if (token[0] == 'X' || token[0] == 'x')
							trail.offset.x = *value;
						else if (token[0] == 'Y' || token[0] == 'y')
							trail.offset.y = *value;
						else if (token[0] == 'Z' || token[0] == 'z')
							trail.offset.z = *value;
					}
		return trail;
	}
	return std::nullopt;
}
}
