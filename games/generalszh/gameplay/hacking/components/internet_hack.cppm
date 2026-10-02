export module games.generalszh.gameplay.hacking.components.internet_hack;
import std;

export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;

// A hacker hacking the internet for its player's cash (HackInternetAIUpdate's HackInternetStateMachine): where it stands
// in unpacking, hacking and packing up, and the frames left of that stage; whether an order came while it hacked or
// packed (m_hasPendingCommand: it goes on once packed; a hack order is a hack again). Simulation state: checkpointed.
export namespace generalszh::gameplay
{
enum class HackStage : std::uint8_t
{
	Idle,
	Unpacking,
	Hacking,
	Packing,
};

enum class HackPending : std::uint8_t
{
	None,
	Order, // the order given goes on once packed (its move held till then)
	Hack,  // hack again once packed
};

struct InternetHack
{
	std::uint32_t framesRemaining{0};
	HackStage stage{HackStage::Idle};
	HackPending pending{HackPending::None};
	std::uint8_t reserved[2]{}; // no padding: checkpoints hold its bytes
};

// Each definition's HackInternetAIUpdate module data (present or not): UnpackTime, PackTime, CashUpdateDelay and
// CashUpdateDelayFast (ticks, rounded up), the cash by veterancy level (Regular, Veteran, Elite, Heroic), XpPerCashUpdate
// and PackUnpackVariationFactor.
struct InternetHackConfig
{
	bool present{false};
	std::uint32_t unpackTicks{0};
	std::uint32_t packTicks{0};
	std::uint32_t cashTicks{0};
	std::uint32_t cashTicksFast{0};
	std::array<std::uint32_t, 4> cash{};
	std::uint32_t xpPerCash{0};
	Engine::Math::Fixed variation;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::InternetHack>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.internet_hack";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
