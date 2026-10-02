export module games.generalszh.content.water.wave_guide_content;
import std;

export import games.generalszh.content.objects.object_definition;
export import engine.time.simulation_time;
export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedAngle;
export import engine.gameplay.rts.death.definitions.death_definition;
import engine.config.binding.values;
import games.generalszh.content.death.death_content;

// The dam break's flood wave (WaveGuideUpdate, GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/
// WaveGuideUpdate.cpp; WaveGuideUpdateModuleData, defaults as there, all 0):
//   WaveDelay (parseDurationReal: milliseconds to frames, the fraction kept: 750 is 22.5 frames), YSize,
//   LinearWaveSpacing, WaveBendMagnitude, WaterVelocity (parseVelocityReal: per second to per frame), PreferredHeight,
//   ShorelineEffectDistance, DamageRadius, DamageAmount, ToppleForce, RandomSplashSound, RandomSplashSoundFrequency,
//   BridgeParticle, BridgeParticleAngleFudge (parseAngleReal: degrees), LoopingSound.
// The shipped data has two: WaveGuide (made by the dam's OCL_DamDie) and WaveGuideGLA01 (placed on the GLA01 map), both
// 750 ms, 650 wide, waves every 15, bent by 500, preferring 37.3 (GLA01's 35) and pushing the water 2.7 (1.0) a second.
// DamDie (DamDie.cpp): nothing of its own beyond DieMuxData's filters (the dam's has none).
export namespace generalszh::content
{
struct WaveGuideContent
{
	Engine::Math::Fixed delayFrames;
	Engine::Math::Fixed ySize;
	Engine::Math::Fixed linearWaveSpacing;
	Engine::Math::Fixed waveBendMagnitude;
	Engine::Math::Fixed waterVelocity; // per frame
	Engine::Math::Fixed preferredHeight;
	Engine::Math::Fixed shorelineEffectDistance;
	Engine::Math::Fixed damageRadius;
	Engine::Math::Fixed damageAmount;
	Engine::Math::Fixed toppleForce;
	std::string randomSplashSound;
	std::int32_t randomSplashSoundFrequency{0};
	std::string bridgeParticle;
	Engine::Math::TurnAngle bridgeParticleAngleFudge{};
	std::string loopingSound;
};

inline std::optional<WaveGuideContent> ReadWaveGuide(const ObjectDefinition &object, const engine::time::FixedStep &step)
{
	using Engine::Math::Fixed;
	for (const ModuleEntry &module : object.modules)
	{
		if (module.block == nullptr || module.type != "WaveGuideUpdate")
			continue;
		const engine::config::Node &block = *module.block;
		WaveGuideContent guide;
		const auto fixed = [&](std::string_view key, Fixed &into) {
			if (const auto *node = block.Find(key); node != nullptr && !node->values.empty())
				into = engine::config::values::ParseFixed(node->Value()).value_or(into);
		};
		const auto text = [&](std::string_view key, std::string &into) {
			if (const auto *node = block.Find(key); node != nullptr && !node->values.empty())
				into = std::string(node->Value());
		};
		Fixed milliseconds;
		fixed("WaveDelay", milliseconds);
		guide.delayFrames = milliseconds * Fixed::FromInt(step.TicksPerSecond()) / Fixed::FromInt(1000);
		fixed("YSize", guide.ySize);
		fixed("LinearWaveSpacing", guide.linearWaveSpacing);
		fixed("WaveBendMagnitude", guide.waveBendMagnitude);
		Fixed perSecond;
		fixed("WaterVelocity", perSecond);
		guide.waterVelocity = step.PerTick(perSecond);
		fixed("PreferredHeight", guide.preferredHeight);
		fixed("ShorelineEffectDistance", guide.shorelineEffectDistance);
		fixed("DamageRadius", guide.damageRadius);
		fixed("DamageAmount", guide.damageAmount);
		fixed("ToppleForce", guide.toppleForce);
		text("RandomSplashSound", guide.randomSplashSound);
		if (const auto *node = block.Find("RandomSplashSoundFrequency"); node != nullptr && !node->values.empty())
			guide.randomSplashSoundFrequency = static_cast<std::int32_t>(engine::config::values::ParseInt(node->Value()).value_or(0));
		text("BridgeParticle", guide.bridgeParticle);
		Fixed degrees;
		fixed("BridgeParticleAngleFudge", degrees);
		guide.bridgeParticleAngleFudge = Engine::Math::TurnFromDegrees(degrees);
		text("LoopingSound", guide.loopingSound);
		return guide;
	}
	return std::nullopt;
}

// DamDie's DieMuxData (DeathTypes, VeterancyLevels, ExemptStatus, RequiredStatus); none: no DamDie.
inline std::optional<engine::gameplay::DeathFilter> ReadDamDie(const ObjectDefinition &object)
{
	for (const ModuleEntry &module : object.modules)
		if (module.type == "DamDie")
			return module.block != nullptr ? death_detail::Filter(*module.block) : engine::gameplay::DeathFilter{};
	return std::nullopt;
}
}
