export module games.generalszh.content.fire.fire_content;
import std;

export import engine.gameplay.common.fire.components.flammable;
export import engine.gameplay.common.fire.components.fire_spread;
export import engine.gameplay.common.fire.resources.fire_settings;
export import games.generalszh.content.objects.object_definition;
export import games.generalszh.content.combat.combat_catalog;

// Zero Hour's fire: FLAME and PARTICLE_BEAM damage set things alight and
// burning deals FLAME damage that kills as BURNED (the original's
// FlammableUpdate); an object's FlammableUpdate module says how it burns,
// and its FireSpreadUpdate how its fire spreads (and the embers it throws).
export namespace generalszh::content
{
engine::gameplay::FireSettings ZeroHourFireSettings()
{
	engine::gameplay::FireSettings settings;
	settings.ignitingDamageTypes = (std::uint64_t{1} << *DamageTypeIndex("FLAME")) | (std::uint64_t{1} << *DamageTypeIndex("PARTICLE_BEAM"));
	settings.burnDamageType = *DamageTypeIndex("FLAME");
	settings.burnDeathType = *DeathTypeIndex("BURNED");
	return settings;
}

struct ObjectFire
{
	engine::gameplay::Flammable flammable;
	std::string burningSound;
};

std::optional<ObjectFire> ReadObjectFire(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || module.type != "FlammableUpdate")
			continue;
		const engine::config::Node &block = *module.block;
		engine::config::Diagnostics diagnostics;
		engine::config::BindContext bind{diagnostics, step};
		const auto ticks = [&](std::string_view key) -> std::uint64_t {
			const auto *node = block.Find(key);
			return node != nullptr ? engine::config::ReadDurationTicks(*node, bind).value_or(0) : 0;
		};
		const auto fixed = [&](std::string_view key, Engine::Math::Fixed fallback) {
			const auto *node = block.Find(key);
			return node != nullptr ? engine::config::values::ParseFixed(node->Value()).value_or(fallback) : fallback;
		};
		// The original's defaults: a limit of 20, gone after 2 seconds without flame.
		ObjectFire fire;
		auto &flammable = fire.flammable;
		flammable.limit = flammable.remaining = fixed("FlameDamageLimit", Engine::Math::Fixed::FromInt(20));
		const auto *expiration = block.Find("FlameDamageExpiration");
		flammable.expiration = expiration != nullptr ? ticks("FlameDamageExpiration") : 2 * step.TicksPerSecond();
		flammable.aflameDuration = ticks("AflameDuration");
		flammable.burnDelay = ticks("AflameDamageDelay");
		flammable.burnAmount = fixed("AflameDamageAmount", Engine::Math::Fixed{});
		flammable.burnedDelay = ticks("BurnedDelay");
		if (const auto *sound = block.Find("BurningSoundName"))
			fire.burningSound = std::string(sound->Value());
		return fire;
	}
	return std::nullopt;
}

struct ObjectFireSpread
{
	engine::gameplay::FireSpread spread;
	std::string embers; // OCLEmbers
};

std::optional<ObjectFireSpread> ReadObjectFireSpread(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.slot != ModuleSlot::Behavior || module.block == nullptr || module.type != "FireSpreadUpdate")
			continue;
		const engine::config::Node &block = *module.block;
		engine::config::Diagnostics diagnostics;
		engine::config::BindContext bind{diagnostics, step};
		const auto ticks = [&](std::string_view key) -> std::uint64_t {
			const auto *node = block.Find(key);
			return node != nullptr ? engine::config::ReadDurationTicks(*node, bind).value_or(0) : 0;
		};
		ObjectFireSpread result;
		result.spread.minDelay = ticks("MinSpreadDelay");
		result.spread.maxDelay = ticks("MaxSpreadDelay");
		if (const auto *range = block.Find("SpreadTryRange"))
			result.spread.range = engine::config::values::ParseFixed(range->Value()).value_or(Engine::Math::Fixed{});
		if (const auto *embers = block.Find("OCLEmbers"))
			result.embers = std::string(embers->Value());
		return result;
	}
	return std::nullopt;
}
}
