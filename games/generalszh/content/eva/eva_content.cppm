export module games.generalszh.content.eva.eva_content;
import std;

export import engine.config.binding.schema;
export import engine.time.simulation_time;

// Eva.ini (the original's EvaCheckInfo, INI::parseEvaEvent): per announcement, its priority (higher wins the voice),
// how long before it may be heard again (TimeBetweenChecksMS) and how long it waits for the voice before it is
// dropped (ExpirationTimeMS), both rounded up to frames; and per side the speech it picks from. The first block of a
// name counts (a later one is ignored). The announcements by the original's EvaMessage order.
export namespace generalszh::content
{
inline constexpr std::array<std::string_view, 53> EvaMessageNames{
	"LOWPOWER", "INSUFFICIENTFUNDS",
	"SUPERWEAPONDETECTED_OWN_PARTICLECANNON", "SUPERWEAPONDETECTED_OWN_NUKE", "SUPERWEAPONDETECTED_OWN_SCUDSTORM",
	"SUPERWEAPONDETECTED_ALLY_PARTICLECANNON", "SUPERWEAPONDETECTED_ALLY_NUKE", "SUPERWEAPONDETECTED_ALLY_SCUDSTORM",
	"SUPERWEAPONDETECTED_ENEMY_PARTICLECANNON", "SUPERWEAPONDETECTED_ENEMY_NUKE", "SUPERWEAPONDETECTED_ENEMY_SCUDSTORM",
	"SUPERWEAPONLAUNCHED_OWN_PARTICLECANNON", "SUPERWEAPONLAUNCHED_OWN_NUKE", "SUPERWEAPONLAUNCHED_OWN_SCUDSTORM",
	"SUPERWEAPONLAUNCHED_ALLY_PARTICLECANNON", "SUPERWEAPONLAUNCHED_ALLY_NUKE", "SUPERWEAPONLAUNCHED_ALLY_SCUDSTORM",
	"SUPERWEAPONLAUNCHED_ENEMY_PARTICLECANNON", "SUPERWEAPONLAUNCHED_ENEMY_NUKE", "SUPERWEAPONLAUNCHED_ENEMY_SCUDSTORM",
	"SUPERWEAPONREADY_OWN_PARTICLECANNON", "SUPERWEAPONREADY_OWN_NUKE", "SUPERWEAPONREADY_OWN_SCUDSTORM",
	"SUPERWEAPONREADY_ALLY_PARTICLECANNON", "SUPERWEAPONREADY_ALLY_NUKE", "SUPERWEAPONREADY_ALLY_SCUDSTORM",
	"SUPERWEAPONREADY_ENEMY_PARTICLECANNON", "SUPERWEAPONREADY_ENEMY_NUKE", "SUPERWEAPONREADY_ENEMY_SCUDSTORM",
	"BUILDINGLOST", "BASEUNDERATTACK", "ALLYUNDERATTACK", "BEACONDETECTED",
	"ENEMYBLACKLOTUSDETECTED", "ENEMYJARMENKELLDETECTED", "ENEMYCOLONELBURTONDETECTED",
	"OWNBLACKLOTUSDETECTED", "OWNJARMENKELLDETECTED", "OWNCOLONELBURTONDETECTED",
	"UNITLOST", "GENERALLEVELUP", "VEHICLESTOLEN", "BUILDINGSTOLEN", "CASHSTOLEN", "UPGRADECOMPLETE", "BUILDINGBEINGSTOLEN",
	"BUILDINGSABOTAGED",
	"SUPERWEAPONLAUNCHED_OWN_GPS_SCRAMBLER", "SUPERWEAPONLAUNCHED_ALLY_GPS_SCRAMBLER", "SUPERWEAPONLAUNCHED_ENEMY_GPS_SCRAMBLER",
	"SUPERWEAPONLAUNCHED_OWN_SNEAK_ATTACK", "SUPERWEAPONLAUNCHED_ALLY_SNEAK_ATTACK", "SUPERWEAPONLAUNCHED_ENEMY_SNEAK_ATTACK",
};

// Eva::nameToMessage (any case); none: not an announcement.
inline std::optional<std::uint32_t> EvaMessageOf(std::string_view name)
{
	for (std::uint32_t index = 0; index < EvaMessageNames.size(); ++index)
		if (name.size() == EvaMessageNames[index].size() &&
			std::equal(name.begin(), name.end(), EvaMessageNames[index].begin(),
				[](char a, char b) { return std::toupper(static_cast<unsigned char>(a)) == std::toupper(static_cast<unsigned char>(b)); }))
			return index;
	return std::nullopt;
}

struct EvaSideSounds
{
	std::string side;
	std::vector<std::string> sounds;
};

struct EvaCheckInfo
{
	std::uint32_t message{0};
	std::uint32_t priority{1};              // Priority (lowest: 1)
	std::uint64_t framesBetweenChecks{900}; // TimeBetweenChecksMS (30 s)
	std::uint64_t framesToExpire{150};      // ExpirationTimeMS (5 s)
	std::vector<EvaSideSounds> sideSounds;  // SideSounds, in order
};

struct EvaCatalog
{
	std::vector<EvaCheckInfo> checks;

	// Eva::getEvaCheckInfo.
	const EvaCheckInfo *Of(std::uint32_t message) const noexcept
	{
		for (const EvaCheckInfo &check : checks)
			if (check.message == message)
				return &check;
		return nullptr;
	}
};

inline EvaCatalog BindEva(const engine::config::Document &document, const engine::time::FixedStep &step)
{
	EvaCatalog catalog;
	// INI::parseDurationUnsignedInt: milliseconds to frames, rounded up.
	const auto frames = [&](const engine::config::Node &field, std::uint64_t fallback) {
		const auto milliseconds = field.values.empty() ? std::nullopt : engine::config::values::ParseInt(field.Value());
		if (!milliseconds || *milliseconds <= 0)
			return milliseconds ? std::uint64_t{0} : fallback;
		const std::uint64_t perSecond = step.TicksPerSecond();
		return (static_cast<std::uint64_t>(*milliseconds) * perSecond + 999u) / 1000u;
	};
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key != "EvaEvent" || root.values.empty())
			continue;
		const auto message = EvaMessageOf(root.Value());
		if (!message || catalog.Of(*message) != nullptr)
			continue;
		EvaCheckInfo info;
		info.message = *message;
		for (const engine::config::Node &field : root.children)
		{
			if (field.key == "Priority" && !field.values.empty())
				info.priority = static_cast<std::uint32_t>(std::max<std::int64_t>(engine::config::values::ParseInt(field.Value()).value_or(1), 0));
			else if (field.key == "TimeBetweenChecksMS")
				info.framesBetweenChecks = frames(field, info.framesBetweenChecks);
			else if (field.key == "ExpirationTimeMS")
				info.framesToExpire = frames(field, info.framesToExpire);
			else if (field.key == "SideSounds")
			{
				EvaSideSounds sounds;
				for (const engine::config::Node &child : field.children)
					if (child.key == "Side" && !child.values.empty())
						sounds.side = std::string(child.Value());
					else if (child.key == "Sounds")
						for (const std::string_view sound : child.values)
							sounds.sounds.emplace_back(sound);
				info.sideSounds.push_back(std::move(sounds));
			}
		}
		catalog.checks.push_back(std::move(info));
	}
	return catalog;
}
}
