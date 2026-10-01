export module engine.gameplay.rts.containment.components.garrison;
export import Engine.Core.Math.Fixed;
import std;

export import engine.ecs.core.component_registry;

// A building units may garrison (the original's GarrisonContain): the team it
// belonged to before anyone moved in (NoTeam: not yet known), whether it heals
// those inside over `fullHealTicks`, and whether it stays garrisonable once
// really damaged (KINDOF_GARRISONABLE_UNTIL_DESTROYED). Held only by stealthy
// garrisoners none has found (KINDOF_STEALTH_GARRISON, not detected), it looks
// to the players its side is not allied with (a bit each in `hiddenFrom`) as
// it did: not garrisoned, still `originalPlayer`'s
// (GarrisonContain::getApparentControllingPlayer).
export namespace engine::gameplay
{
struct Garrison
{
	static constexpr std::uint32_t NoTeam = 0xFFFFFFFFu;
	std::uint32_t originalTeam{NoTeam};
	std::uint32_t rosterDefinition{NoTeam}; // InitialRoster: what it starts with inside (a definition id), still to make
	std::uint64_t fullHealTicks{0}; // 0: it does not heal them
	std::uint32_t rosterLeft{0};
	std::uint32_t originalPlayer{NoTeam};
	std::uint32_t hiddenFrom{0};
	bool untilDestroyed{false};
	std::uint8_t immuneToClear{0}; // ImmuneToClearBuildingAttacks: projectiles that clear garrisons do not here
	// setEvacDisposition (a script's NAMED_SET_EVAC_LEFT_OR_RIGHT): 0 bursting from its centre, 1 out to its left, 2 to its
	// right; and its half length and width (its geometry's major and minor radius) the sides are measured by.
	std::uint8_t evac{0};
	std::uint8_t padding{0};
	Engine::Math::Fixed halfLength;
	Engine::Math::Fixed halfWidth;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Garrison>
{
	static constexpr std::string_view StableName = "engine.gameplay.garrison";
	static constexpr std::uint32_t Version = 2;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Garrison &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.originalTeam);
		hasher.AppendU64(value.fullHealTicks);
		hasher.AppendU64(value.untilDestroyed ? 1u : 0u);
		hasher.AppendU64((std::uint64_t{value.rosterDefinition} << 32) | value.rosterLeft);
		hasher.AppendU64((std::uint64_t{value.originalPlayer} << 32) | value.hiddenFrom);
		hasher.AppendU64(value.evac);
		hasher.AppendU64(static_cast<std::uint64_t>(value.halfLength.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.halfWidth.Raw()));
	}
};
}
