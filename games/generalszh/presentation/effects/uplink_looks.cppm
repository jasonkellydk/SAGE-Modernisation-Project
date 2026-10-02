export module games.generalszh.presentation.effects.uplink_looks;
import std;

export import games.generalszh.content.objects.object_definition;
export import games.generalszh.presentation.effects.laser_looks;
import games.generalszh.content.powers.special_powers;

// The Particle Cannon uplink's client effects (ParticleUplinkCannonUpdate's module data): its outer nodes (the
// OuterEffectNumBones bones OuterEffectBoneName01..) and their light, medium and intense flares; the connector bone
// where the connector lasers from the nodes meet, their medium and intense lasers and flares; the fire bone the beam
// leaves from and its light, medium and intense base flares; the orbital beam (ParticleBeamLaserName); and its four
// sound loops (PoweringUpSoundLoop, UnpackToIdleSoundLoop, FiringToPackSoundLoop, GroundAnnihilationSoundLoop); and
// WidthGrowTime (logic frames), over which its ground-to-orbit laser widens and narrows.
export namespace generalszh::content
{
enum class UplinkIntensity : std::uint8_t
{
	Light,
	Medium,
	Intense,
};

enum class UplinkSound : std::uint8_t
{
	PoweringUp,
	UnpackToIdle,
	FiringToPack,
	GroundAnnihilation,
};

inline constexpr std::size_t UplinkSoundCount = 4;
inline constexpr std::uint32_t MaxUplinkOuterNodes = 16; // MAX_OUTER_NODES

struct UplinkLook
{
	std::string outerBone;
	std::uint32_t outerBones{0};
	std::array<std::string, 3> outerFlares; // by UplinkIntensity
	std::string connectorBone;
	std::array<std::optional<LaserLook>, 3> connectorLasers; // medium and intense only
	std::array<std::string, 3> connectorFlares;              // medium and intense only
	std::string fireBone;
	std::array<std::string, 3> baseFlares;
	std::optional<LaserLook> beam;
	std::array<std::string, UplinkSoundCount> sounds;
	std::uint64_t widthGrowTicks{0};
};

// `named` finds a laser object by its name (none: nullptr).
inline std::optional<UplinkLook> ReadUplinkLook(const ObjectDefinition &object, const std::function<const ObjectDefinition *(std::string_view)> &named)
{
	for (const ModuleEntry &module : object.modules)
	{
		if (module.type != "ParticleUplinkCannonUpdate" || module.block == nullptr)
			continue;
		const auto text = [&](std::string_view key) {
			const auto *node = module.block->Find(key);
			return node != nullptr ? std::string(node->Value()) : std::string{};
		};
		const auto laser = [&](std::string_view key) -> std::optional<LaserLook> {
			const std::string name = text(key);
			const ObjectDefinition *found = name.empty() ? nullptr : named(name);
			return found != nullptr ? ReadLaserLook(*found) : std::nullopt;
		};
		UplinkLook look;
		look.outerBone = text("OuterEffectBoneName");
		std::uint32_t bones = 0;
		const std::string count = text("OuterEffectNumBones");
		std::from_chars(count.data(), count.data() + count.size(), bones);
		look.outerBones = std::min(bones, MaxUplinkOuterNodes);
		look.outerFlares = {text("OuterNodesLightFlareParticleSystem"), text("OuterNodesMediumFlareParticleSystem"),
			text("OuterNodesIntenseFlareParticleSystem")};
		look.connectorBone = text("ConnectorBoneName");
		look.connectorLasers = {std::nullopt, laser("ConnectorMediumLaserName"), laser("ConnectorIntenseLaserName")};
		look.connectorFlares = {std::string{}, text("ConnectorMediumFlare"), text("ConnectorIntenseFlare")};
		look.fireBone = text("FireBoneName");
		look.baseFlares = {text("LaserBaseLightFlareParticleSystemName"), text("LaserBaseMediumFlareParticleSystemName"),
			text("LaserBaseIntenseFlareParticleSystemName")};
		look.beam = laser("ParticleBeamLaserName");
		if (const auto cannon = ReadParticleCannon(object, engine::time::FixedStep{30}))
			look.widthGrowTicks = cannon->widthGrowTicks;
		look.sounds = {text("PoweringUpSoundLoop"), text("UnpackToIdleSoundLoop"), text("FiringToPackSoundLoop"), text("GroundAnnihilationSoundLoop")};
		return look;
	}
	return std::nullopt;
}
}
