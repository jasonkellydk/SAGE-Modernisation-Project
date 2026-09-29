export module games.generalszh.gameplay.crates.components.pilot_seeker;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import Engine.Core.Math.Fixed;
import games.generalszh.content.objects.kind_of;
import engine.ecs.system.system;

// A pilot looking for a vehicle to climb into (PilotFindVehicleUpdate, with its VeterancyCrateCollide): how often and
// how far it looks, how healthy the vehicle must be, which vehicles it may join (their kinds; its own player's, not
// flying, when a pilot), how many levels it brings (its own, or one), when it looks next and the vehicle it is going
// for.
export namespace generalszh::gameplay
{
struct PilotSeeker
{
	std::uint64_t scanTicks{0};
	std::uint64_t nextScan{0};
	Engine::Math::Fixed range;
	Engine::Math::Fixed minHealth;
	content::KindOfMask required{};
	content::KindOfMask forbidden{};
	ecs::Entity goal;
	std::uint8_t addsOwnerVeterancy{0};
	std::uint8_t isPilot{0};
	std::uint16_t reserved{0};
	std::uint32_t reserved2{0};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::PilotSeeker>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.pilot_seeker";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const generalszh::gameplay::PilotSeeker &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(value.scanTicks);
		hasher.AppendU64(value.nextScan);
		hasher.AppendU64(static_cast<std::uint64_t>(value.range.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.minHealth.Raw()));
		for (const std::uint64_t word : value.required)
			hasher.AppendU64(word);
		for (const std::uint64_t word : value.forbidden)
			hasher.AppendU64(word);
		hasher.AppendU64(value.goal.index);
		hasher.AppendU64(value.goal.generation);
		hasher.AppendU64(value.addsOwnerVeterancy | (static_cast<std::uint64_t>(value.isPilot) << 8));
	}
};
}
