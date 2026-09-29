export module engine.gameplay.rts.propaganda.systems.propaganda_system;
import std;

export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import engine.gameplay.rts.propaganda.components.propaganda;
export import engine.gameplay.rts.propaganda.resources.propaganda_scans;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.spatial.components.targetable;
export import engine.gameplay.common.spatial.resources.spatial_index;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.health.components.health;
export import engine.gameplay.common.status.components.disabled;
export import engine.gameplay.common.status.components.status_flags;
export import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.healing.resources.heal_pulses;
export import engine.gameplay.rts.upgrades.resources.player_upgrades;
export import engine.gameplay.rts.construction.components.under_construction;
export import engine.gameplay.rts.construction.components.sale;

// PropagandaTowerBehavior (GeneralsMD/Code/GameEngine/Source/GameLogic/Object/Behavior/PropagandaTowerBehavior.cpp):
// PropagandaScanSystem: each tower works out whether it may work this tick (update: under construction, sold, dead,
// disabled but held, carried inside something, neutral), and one that may, `delay` ticks after its last scan, takes in
// the allies within its radius (doScan) and plays its pulse (upgraded or not).
// PropagandaInfluenceSystem, in parallel: each thing that may be influenced follows its tower (update's effectLogic):
// let go when its tower stops or leaves it out of a scan (its bonuses cleared), taken by the first tower that took it
// in, and while it has a working tower given its bonuses (if armed: hasAnyDamageWeapon) and healed as that tower's
// sole benefactor (attemptHealingFromSoleBenefactor, for the tower's scan delay).
export namespace engine::gameplay
{
struct PropagandaScanSystem
{
	using Query = ecs::Query<ecs::Optional<TeamMember>, ecs::Write<PropagandaTower>, ecs::Read<Transform>, ecs::Read<Owner>, ecs::Read<Health>, ecs::Optional<Disabled>,
		ecs::Optional<OffMap>, ecs::Optional<StatusFlags>, ecs::Optional<UnderConstruction>, ecs::Optional<Sale>>;
	using Resources = ecs::Resources<ecs::Read<SpatialIndex>, ecs::Read<Relationships>, ecs::Read<PlayerUpgrades>, ecs::Write<PropagandaScans>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		const SpatialIndex &spatial = context.Read<SpatialIndex>();
		const Relationships &relationships = context.Read<Relationships>();
		const PlayerUpgrades &upgrades = context.Read<PlayerUpgrades>();
		PropagandaScans &scans = context.Write<PropagandaScans>();
		scans.Clear();
		const std::uint64_t tick = context.Tick();
		query.ForEachChunk([&](auto chunk) {
			auto towers = chunk.template Get<PropagandaTower>();
			const auto transforms = chunk.template Get<Transform>();
			const auto owners = chunk.template Get<Owner>();
			const auto teamRows = chunk.template Get<TeamMember>();
			const auto healths = chunk.template Get<Health>();
			const auto disabled = chunk.template Get<Disabled>();
			const auto away = chunk.template Get<OffMap>();
			const auto status = chunk.template Get<StatusFlags>();
			const auto building = chunk.template Get<UnderConstruction>();
			const auto selling = chunk.template Get<Sale>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < towers.size(); ++row)
			{
				PropagandaTower &tower = towers[row];
				const std::uint32_t player = owners[row].player;
				const std::uint32_t team = teamRows.empty() ? Relationships::NoTeam : teamRows[row].team;
				bool active = !IsDead(healths[row]) && player != tower.neutralPlayer;
				if ((!status.empty() && (status[row].bits & tower.pausedStatus) != 0) || !building.empty() || !selling.empty())
					active = false;
				if (!disabled.empty() && (disabled[row].mask & ~disabled_type::Held) != 0)
					active = false;
				// Carried inside something (getEnclosingContainedBy); mounted on top (a portable structure) it still works.
				if (!away.empty() && away[row].reason == 1)
					active = false;
				tower.active = active ? 1 : 0;
				if (!active || tick - tower.lastScan < tower.delay)
					continue;
				tower.lastScan = tick;
				const auto at = transforms[row].position;
				const bool upgraded = tower.upgrade != PropagandaTower::NoUpgrade && upgrades.Completed(player).Has(tower.upgrade);
				const std::uint32_t effect = upgraded ? tower.upgradedPulseEffect : tower.pulseEffect;
				if (effect != PropagandaTower::NoEffect)
					scans.pulses.push_back({entities[row], effect, at});
				scans.scanned.push_back(entities[row]);
				spatial.ForEachWithin(at.XY(), tower.radius, [&](const SpatialEntry &entry) {
					if (Engine::Math::DistanceSquared(entry.position.XY(), at.XY()) > tower.radius * tower.radius)
						return;
					if ((entry.classes & target_class::Structure) != 0)
						return;
					if (entry.entity == entities[row] && tower.affectsSelf == 0)
						return;
					if (!relationships.Allies(team, player, entry.team, entry.player))
						return;
					scans.takenIn.emplace_back(PropagandaScans::Key(entry.entity), entities[row]);
				});
			}
		});
		const auto byKey = [](ecs::Entity a, ecs::Entity b) { return PropagandaScans::Key(a) < PropagandaScans::Key(b); };
		std::sort(scans.scanned.begin(), scans.scanned.end(), byKey);
		std::sort(scans.takenIn.begin(), scans.takenIn.end(), [](const auto &a, const auto &b) {
			return a.first != b.first ? a.first < b.first : PropagandaScans::Key(a.second) < PropagandaScans::Key(b.second);
		});
	}
};

struct PropagandaInfluenceSystem
{
	using Query = ecs::Query<ecs::Write<PropagandaInfluence>, ecs::Read<Health>, ecs::OptionalWrite<WeaponBonusConditions>, ecs::Optional<Armament>,
		ecs::Read<Transform>>;
	using Lookup = ecs::Lookup<ecs::Read<PropagandaTower>, ecs::Read<Owner>>;
	using Resources = ecs::Resources<ecs::Read<PropagandaScans>, ecs::Read<PlayerUpgrades>, ecs::Write<PropagandaHeals>, ecs::Write<HealPulses>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<PropagandaHeals>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const PropagandaScans &scans = context.Read<PropagandaScans>();
		const PlayerUpgrades &upgrades = context.Read<PlayerUpgrades>();
		const auto lookup = context.Lookup<Lookup>();
		auto &heals = context.Write<PropagandaHeals>().Slot(context);
		const std::uint64_t tick = context.Tick();
		const auto perSecond = Engine::Math::Fixed::FromInt(static_cast<std::int32_t>(context.Time().Step().TicksPerSecond()));
		auto influences = chunk.Get<PropagandaInfluence>();
		const auto healths = chunk.Get<Health>();
		auto bonuses = chunk.Get<WeaponBonusConditions>();
		const auto armaments = chunk.Get<Armament>();
		const auto transforms = chunk.Get<Transform>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < influences.size(); ++row)
		{
			PropagandaInfluence &influence = influences[row];
			WeaponBonusConditions *bonus = bonuses.empty() ? nullptr : &bonuses[row];
			const auto taken = scans.TakenIn(entities[row]);
			const auto letGo = [&](const PropagandaTower *tower) {
				if (bonus != nullptr && tower != nullptr)
					SetWeaponBonus(*bonus, tower->bonus | tower->upgradedBonus, false, tick);
				influence.tower = {};
			};
			if (influence.tower.IsValid())
			{
				const PropagandaTower *tower = lookup.IsAlive(influence.tower) ? lookup.Get<PropagandaTower>(influence.tower) : nullptr;
				if (tower == nullptr || tower->active == 0)
					letGo(tower);
				else if (scans.Scanned(influence.tower) &&
					std::none_of(taken.begin(), taken.end(), [&](const auto &entry) { return entry.second == influence.tower; }))
					letGo(tower);
			}
			if (!influence.tower.IsValid() && !taken.empty())
				influence.tower = taken.front().second;
			if (!influence.tower.IsValid() || IsDead(healths[row]))
				continue;
			const PropagandaTower *tower = lookup.Get<PropagandaTower>(influence.tower);
			const Owner *owner = lookup.Get<Owner>(influence.tower);
			if (tower == nullptr || owner == nullptr)
				continue;
			const bool upgraded = tower->upgrade != PropagandaTower::NoUpgrade && upgrades.Completed(owner->player).Has(tower->upgrade);
			if (bonus != nullptr && !armaments.empty() && armaments[row].weapon != 0xFFFFFFFFu)
				SetWeaponBonus(*bonus, tower->bonus | (upgraded ? tower->upgradedBonus : 0u), true, tick);
			const Engine::Math::Fixed share = upgraded ? tower->upgradedHeal : tower->heal;
			if (share > Engine::Math::Fixed{})
				heals.push_back({entities[row], influence.tower, healths[row].maximum * share / perSecond, tower->delay, transforms[row].position});
		}
	}

	void AfterChunks(Query &, ecs::SystemContext &context) const
	{
		HealPulses &pulses = context.Write<HealPulses>();
		context.Write<PropagandaHeals>().ForEach([&](const HealPulse &pulse) { pulses.Add(pulse); });
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::PropagandaScanSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.propaganda_scan";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};

template<>
struct SystemTraits<engine::gameplay::PropagandaInfluenceSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.propaganda_influence";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	// The game orders it after area healing gathers the tick's heal pulses and before they are applied.
	using Before = SystemTypeList<>;
	using After = SystemTypeList<engine::gameplay::PropagandaScanSystem>;
};
}
