export module engine.gameplay.rts.economy.components.auto_deposit;
import std;

export import engine.ecs.core.entity;
export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedVector;
import engine.ecs.system.system;

// Something that pays its player every so often (the original's AutoDepositUpdate: an oil derrick, a black market): every
// `period` ticks (DepositTiming) `amount` (DepositAmount), plus `boost` while its player has `boostUpgrade` complete
// (UpgradedBoost); counted money only when `actualMoney` (a fake building only shows it). Its first payday arms the
// one-time `captureBonus` (InitialCaptureBonus) that a player taking it over is paid (Player::becomingTeamMember ->
// awardInitialCaptureBonus); every change to a playable owner restarts its period. Nothing for the neutral player or
// while unfinished. `player`: the owner it last saw. Simulation state: checkpointed.
// AutoDeposits: this tick's payments, for the floating "+$" text (display-only ones too). Not simulation state.
export namespace engine::gameplay
{
struct AutoDeposit
{
	static constexpr std::uint32_t NoUpgrade = 0xFFFFFFFFu;
	std::uint64_t nextTick{0};
	std::uint64_t period{0};
	std::int64_t amount{0};
	std::int64_t captureBonus{0};
	std::int64_t boost{0};
	std::uint32_t boostUpgrade{NoUpgrade};
	std::uint32_t player{0};
	std::uint8_t actualMoney{1};
	std::uint8_t initialized{0};
	std::uint8_t awardCapture{0};
	std::uint8_t reserved[5]{};
};

struct AutoDepositPayment
{
	ecs::Entity entity;
	std::uint32_t player{0};
	std::uint32_t capture{0}; // the capture bonus (floats over it unscattered)
	std::int64_t amount{0};
	Engine::Math::FixedVector3 position;
};

struct AutoDeposits
{
	std::vector<AutoDepositPayment> list;
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::AutoDeposit>
{
	static constexpr std::string_view StableName = "engine.gameplay.auto_deposit";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ResourceTraits<engine::gameplay::AutoDeposits>
{
	static constexpr std::string_view StableName = "engine.gameplay.auto_deposits";
};
}
