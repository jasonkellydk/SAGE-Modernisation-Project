export module engine.gameplay.rts.upgrades.systems.upgrade_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.upgrades.components.upgradable;
export import engine.gameplay.rts.upgrades.resources.upgrade_triggers;
export import engine.gameplay.rts.upgrades.resources.player_upgrades;
export import engine.gameplay.rts.upgrades.resources.upgrade_reactions;
export import engine.gameplay.rts.upgrades.resources.object_upgrade_grants;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.rts.construction.components.under_construction;

// Object::updateUpgradeModules, chunk-parallel, after the tick's production
// (as the original, within the frame an upgrade completes): an object looks at
// its upgrade triggers when it is new, was given an upgrade of its own (by a
// script, or this tick's ObjectUpgradeGrants), changed hands, or its player
// completed an upgrade. Nothing upgrades while it is under construction (it
// looks again once built: the dozer's updateUpgradeModules as it finishes).
// Against its player's and its own
// upgrades together (one key for the whole pass, as the original), each
// trigger not yet gone that would upgrade goes, in module order: first its
// removals (the object's own upgrades; triggers they set off may go again,
// UpgradeMux::resetUpgrade), then its reaction, for the game to carry out.
export namespace engine::gameplay
{
// Object::removeUpgrade: `upgrades` off the object, and the triggers they would set off (UpgradeMux::resetUpgrade: its
// activation among them) may go again. Their effects are not undone.
inline void RemoveObjectUpgrades(Upgradable &object, const std::vector<UpgradeTrigger> &triggers, const UpgradeMask &upgrades)
{
	object.completed.Remove(upgrades);
	for (std::size_t index = 0; index < triggers.size() && index < 32; ++index)
		if (triggers[index].activation.AnyOf(upgrades))
			object.executed &= ~(1u << index);
}

namespace upgrade_detail
{
// One pass (updateUpgradeModules) over an object's triggers; `react` gets each trigger that goes.
template<typename React>
void UpdateUpgradeTriggers(Upgradable &object, const std::vector<UpgradeTrigger> &triggers, const UpgradeMask &playerUpgrades, React &&react)
{
	UpgradeMask key = playerUpgrades;
	key.Add(object.completed);
	for (std::size_t index = 0; index < triggers.size() && index < 32; ++index)
	{
		const UpgradeTrigger &trigger = triggers[index];
		if (!WouldUpgrade(trigger, key, (object.executed >> index & 1u) != 0))
			continue;
		if (trigger.removal.Any())
			RemoveObjectUpgrades(object, triggers, trigger.removal);
		object.executed |= 1u << index;
		react(static_cast<std::uint32_t>(index), trigger.reaction);
	}
}
}

struct UpgradeSystem
{
	using Query = ecs::Query<ecs::Write<Upgradable>, ecs::Read<DefinitionRef>, ecs::Read<Owner>, ecs::Optional<UnderConstruction>>;
	using Resources = ecs::Resources<ecs::Read<UpgradeTriggers>, ecs::Read<PlayerUpgrades>, ecs::Write<ObjectUpgradeGrants>, ecs::Write<UpgradeReactions>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<UpgradeReactions>().Reset(query.PreparedChunkCount()); }
	// This tick's grants are taken.
	void AfterChunks(Query &, ecs::SystemContext &context) { context.Write<ObjectUpgradeGrants>().list.clear(); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const UpgradeTriggers &definitions = context.Read<UpgradeTriggers>();
		const PlayerUpgrades &players = context.Read<PlayerUpgrades>();
		const ObjectUpgradeGrants &grants = context.Write<ObjectUpgradeGrants>();
		auto &out = context.Write<UpgradeReactions>().Slot(context);
		auto objects = chunk.Get<Upgradable>();
		const auto refs = chunk.Get<DefinitionRef>();
		const auto owners = chunk.Get<Owner>();
		const auto building = chunk.Get<UnderConstruction>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < objects.size(); ++row)
		{
			Upgradable &object = objects[row];
			for (const ObjectUpgradeGrant &grant : grants.list)
				if (grant.entity == entities[row])
				{
					object.completed.Set(grant.upgrade); // Object::giveUpgrade
					object.stale = 1;
				}
			// Object::updateUpgradeModules: none while OBJECT_STATUS_UNDER_CONSTRUCTION; it looks once that clears.
			if (!building.empty())
			{
				object.stale = 1;
				continue;
			}
			const std::uint32_t player = owners[row].player;
			const std::uint32_t grants = players.Grants(player);
			if (object.stale == 0 && object.seenPlayer == player && object.seenGrants == grants)
				continue;
			object.stale = 0;
			object.seenPlayer = player;
			object.seenGrants = grants;
			const std::vector<UpgradeTrigger> *triggers = definitions.Of(refs[row].index);
			if (triggers == nullptr)
				continue;
			upgrade_detail::UpdateUpgradeTriggers(object, *triggers, players.Completed(player), [&](std::uint32_t trigger, std::uint32_t reaction) {
				out.push_back({entities[row], trigger, reaction});
			});
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::UpgradeSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.upgrades";
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
