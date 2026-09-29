export module games.generalszh.gameplay.production.algorithms.build_cost;
import std;

export import games.generalszh.gameplay.world.resources.game_world;
export import games.generalszh.content.objects.object_definition;
import games.generalszh.gameplay.production.components.cost_modifying;
import games.generalszh.gameplay.upgrades.resources.upgrade_effects;
import engine.gameplay.rts.upgrades.components.upgradable;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.definition_ref;
import engine.ecs.query.query;

// ThingTemplate::calcCostToBuild(player): BuildCost times the player's kind-of cost changes
// (getProductionCostChangeBasedOnKindOf: the product of 1 + Percentage over its changes whose EffectKindOf the thing
// has all of). Its changes are Player::m_kindOfPercentProductionChangeList: one per distinct EffectKindOf and Percentage
// among its objects whose CostModifierUpgrade has gone (addKindOfProductionCostChange counts a repeat by reference, so
// two such objects give one change); the object's controlling player holds it (onCapture moves it) until the object
// is deleted (onDelete). The faction's per-name ProductionCostChange and the map's BUILDCOST handicap are 1 for every
// shipped side and map. The original truncates its float product; here the exact product is floored (the same whole
// numbers for the shipped -10% on every build cost).
export namespace generalszh::gameplay
{
inline constexpr std::int64_t CostShareWhole = 10000; // 100% in hundredths of a percent

// A player's kind-of cost changes (Player::m_kindOfPercentProductionChangeList), gathered once for several costs.
struct CostChanges
{
	std::vector<std::pair<content::KindOfMask, std::int64_t>> changes;

	// calcCostToBuild: the exact product, floored once at the end (the changes multiply before any truncation).
	std::int64_t CostOf(const content::ObjectDefinition &what) const noexcept
	{
		std::int64_t cost = what.buildCost, divisor = 1;
		for (const auto &[kinds, share] : changes)
		{
			bool all = true;
			for (std::size_t word = 0; word < kinds.size(); ++word)
				all = all && (what.kinds[word] & kinds[word]) == kinds[word];
			if (!all)
				continue;
			cost *= CostShareWhole + share;
			divisor *= CostShareWhole;
		}
		return cost >= 0 ? cost / divisor : -((-cost + divisor - 1) / divisor);
	}
};

inline CostChanges CostChangesOf(GameWorld &game, std::uint32_t player)
{
	namespace gp = engine::gameplay;
	CostChanges out;
	const UpgradeEffects *effects = game.world.FindResource<UpgradeEffects>();
	if (effects == nullptr)
		return out;
	auto &changes = out.changes;
	ecs::Query<ecs::Read<CostModifying>, ecs::Read<gp::Upgradable>, ecs::Read<gp::Owner>, ecs::Read<gp::DefinitionRef>> modifiers(game.world);
	modifiers.ForEachChunk([&](auto chunk) {
		const auto modifying = chunk.template Get<CostModifying>();
		const auto upgradables = chunk.template Get<gp::Upgradable>();
		const auto owners = chunk.template Get<gp::Owner>();
		const auto definitions = chunk.template Get<gp::DefinitionRef>();
		for (std::size_t row = 0; row < modifying.size(); ++row)
		{
			if (owners[row].player != player)
				continue;
			const std::uint32_t gone = modifying[row].triggers & upgradables[row].executed;
			for (std::uint32_t trigger = 0; trigger < 32; ++trigger)
				if ((gone >> trigger & 1u) != 0)
					if (const UpgradeEffect *effect = effects->Of(definitions[row].index, trigger))
					{
						const std::pair<content::KindOfMask, std::int64_t> change{effect->kinds, effect->share};
						if (std::ranges::find(changes, change) == changes.end())
							changes.push_back(change);
					}
		}
	});
	return out;
}

inline std::int64_t CostToBuild(GameWorld &game, std::uint32_t player, const content::ObjectDefinition &what)
{
	return CostChangesOf(game, player).CostOf(what);
}
}
