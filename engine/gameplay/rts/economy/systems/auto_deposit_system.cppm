export module engine.gameplay.rts.economy.systems.auto_deposit_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.economy.components.auto_deposit;
export import engine.gameplay.rts.economy.resources.player_money;
export import engine.gameplay.rts.upgrades.resources.player_upgrades;
export import engine.gameplay.rts.teams.resources.team_roster;
export import engine.gameplay.rts.construction.components.construction_progress;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.spatial.components.transform;

// AutoDepositUpdate (GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Update/AutoDepositUpdate.cpp), see AutoDeposit:
// an owner change to a playable player first (awardInitialCaptureBonus: the period restarts; the armed bonus is paid
// once); then update: on its payday the next is set a period on (its first arms the capture bonus), and a finished one
// of a playable player pays amount plus boost. The original's getUpgradedSupplyBoost looked every pair up through one
// function-static template (the first ever found, whatever object asked); each object's own upgrade is used here, its
// first pair (the shipped data has one: TechOilDerrick's Upgrade_AmericaSupplyLines).
export namespace engine::gameplay
{
struct AutoDepositSystem
{
	using Query = ecs::Query<ecs::Write<AutoDeposit>, ecs::Read<Owner>, ecs::Read<Transform>, ecs::Optional<ConstructionProgress>>;
	using Resources = ecs::Resources<ecs::Read<TeamRoster>, ecs::Read<PlayerUpgrades>, ecs::Write<PlayerMoney>, ecs::Write<AutoDeposits>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const TeamRoster &roster = context.Read<TeamRoster>();
		const PlayerUpgrades &upgrades = context.Read<PlayerUpgrades>();
		PlayerMoney &money = context.Write<PlayerMoney>();
		auto &paid = context.Write<AutoDeposits>().list;
		paid.clear();
		const std::uint64_t now = context.Tick();
		const auto neutral = [&](std::uint32_t player) { return player < roster.PlayerCount() && roster.PlayerAt(player).name.empty(); };
		query.ForEachChunk([&](auto chunk) {
			auto deposits = chunk.template Get<AutoDeposit>();
			const auto owners = chunk.template Get<Owner>();
			const auto transforms = chunk.template Get<Transform>();
			const auto progress = chunk.template Get<ConstructionProgress>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < deposits.size(); ++row)
			{
				AutoDeposit &deposit = deposits[row];
				const std::uint32_t player = owners[row].player;
				Engine::Math::FixedVector3 above = transforms[row].position;
				above.z += Engine::Math::Fixed::FromInt(10);
				if (player != deposit.player)
				{
					deposit.player = player;
					if (!neutral(player))
					{
						deposit.nextTick = now + deposit.period;
						if (deposit.awardCapture != 0 && deposit.captureBonus > 0)
						{
							money.Earn(player, deposit.captureBonus);
							paid.push_back({entities[row], player, 1, deposit.captureBonus, above});
							deposit.awardCapture = 0;
						}
					}
				}
				if (now < deposit.nextTick)
					continue;
				if (deposit.initialized == 0)
				{
					deposit.awardCapture = 1;
					deposit.initialized = 1;
				}
				deposit.nextTick = now + deposit.period;
				if (neutral(player) || deposit.amount <= 0)
					continue;
				if (!progress.empty() && progress[row].percent < Engine::Math::Fixed::FromInt(100))
					continue;
				std::int64_t amount = deposit.amount;
				if (deposit.boostUpgrade != AutoDeposit::NoUpgrade && upgrades.Completed(player).Has(deposit.boostUpgrade))
					amount += deposit.boost;
				// (Earned: its amount without the boost, as the original counts it.)
				if (deposit.actualMoney != 0)
				{
					money.Deposit(player, amount);
					money.AddEarned(player, deposit.amount);
				}
				if (amount > 0)
					paid.push_back({entities[row], player, 0, amount, above});
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::AutoDepositSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.auto_deposits";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
