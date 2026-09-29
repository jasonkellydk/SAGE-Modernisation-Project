export module engine.gameplay.rts.horde.systems.horde_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.horde.components.horde;
export import engine.gameplay.rts.horde.resources.horde_catalog;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.spatial.components.off_map;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.resources.relationships;
export import engine.gameplay.common.weapons.components.weapon_bonus_conditions;
export import engine.gameplay.rts.upgrades.resources.player_upgrades;

// HordeUpdate::update, chunk-parallel: on its look tick (infantry every
// UpdateRate, others every UpdateRate + 1, as the original's frame test) a
// unit counts its fellows within Radius (FROM_BOUNDINGSPHERE_3D: the bounding
// spheres' distance): hordes of its kinds (all of KindOf), its allies when
// AlliesOnly, its own type when ExactMatch; units being carried are not in the
// partition. Count - 1 fellows make it a true member; fewer, it is still in the
// horde within RubOffRadius (centre to centre, 2D) of a fellow that was a true
// member. Then AIUpdate's evaluateMoraleBonus (HORDEACTION_HORDE, the retail
// action): the horde bonus while in the horde, and the catalog's upgrade rule
// (when the definition allows it). Fellows are read as they were before this
// tick's looks. Retail counts dead fellows too.
export namespace engine::gameplay
{
struct HordeSystem
{
	using Query = ecs::Query<ecs::Optional<TeamMember>, ecs::Write<Horde>, ecs::Read<Transform>, ecs::Read<DefinitionRef>, ecs::Read<Owner>, ecs::OptionalWrite<WeaponBonusConditions>,
		ecs::Optional<OffMap>>;
	using Resources = ecs::Resources<ecs::Read<HordeCatalog>, ecs::Write<HordeRoster>, ecs::Read<Relationships>, ecs::Read<PlayerUpgrades>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const
	{
		const HordeCatalog &catalog = context.Read<HordeCatalog>();
		auto &entries = context.Write<HordeRoster>().entries;
		entries.clear();
		query.ForEachPreparedChunk([&](auto chunk) {
			if (!chunk.template Get<OffMap>().empty())
				return; // carried: out of the partition
			const auto hordes = chunk.template Get<Horde>();
			const auto transforms = chunk.template Get<Transform>();
			const auto definitions = chunk.template Get<DefinitionRef>();
			const auto owners = chunk.template Get<Owner>();
			const auto teamRows = chunk.template Get<TeamMember>();
			const auto entities = chunk.Entities();
			for (std::size_t row = 0; row < hordes.size(); ++row)
			{
				const HordeDefinition *definition = catalog.Of(definitions[row].index);
				if (definition == nullptr)
					continue;
				const auto &at = transforms[row].position;
				entries.push_back({entities[row].index, entities[row].generation, definitions[row].index, owners[row].player, at.x, at.y,
					at.z + definition->centerHeight, hordes[row].trueMember, teamRows.empty() ? Relationships::NoTeam : teamRows[row].team});
			}
		});
	}

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		using Engine::Math::Fixed;
		const HordeCatalog &catalog = context.Read<HordeCatalog>();
		const HordeRoster &roster = context.Write<HordeRoster>();
		const Relationships &relationships = context.Read<Relationships>();
		const PlayerUpgrades &upgrades = context.Read<PlayerUpgrades>();
		const std::uint64_t tick = context.Tick();
		auto hordes = chunk.Get<Horde>();
		const auto transforms = chunk.Get<Transform>();
		const auto definitions = chunk.Get<DefinitionRef>();
		const auto owners = chunk.Get<Owner>();
		const auto teamRows = chunk.Get<TeamMember>();
		auto bonuses = chunk.Get<WeaponBonusConditions>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < hordes.size(); ++row)
		{
			Horde &horde = hordes[row];
			if (tick < horde.nextTick)
				continue;
			const HordeDefinition *mine = catalog.Of(definitions[row].index);
			if (mine == nullptr)
				continue;
			horde.nextTick = tick + mine->updateTicks + (mine->infantry ? 0u : 1u);
			const auto &at = transforms[row].position;
			const Fixed centerZ = at.z + mine->centerHeight;
			const std::uint32_t player = owners[row].player;
			const std::uint32_t team = teamRows.empty() ? Relationships::NoTeam : teamRows[row].team;
			std::uint32_t fellows = 0;
			bool rubbedOff = false;
			const Fixed rubOff = mine->rubOffRadius * mine->rubOffRadius;
			for (const HordeRosterEntry &other : roster.entries)
			{
				if (other.entityIndex == entities[row].index && other.entityGeneration == entities[row].generation)
					continue;
				const HordeDefinition &theirs = catalog.byDefinition[other.definition];
				// PartitionFilterHordeMember.
				if (mine->exactMatch && other.definition != definitions[row].index)
					continue;
				if ((theirs.kinds[0] & mine->requiredKinds[0]) != mine->requiredKinds[0] || (theirs.kinds[1] & mine->requiredKinds[1]) != mine->requiredKinds[1])
					continue;
				if (mine->alliesOnly && !relationships.Allies(team, player, other.team, other.player))
					continue;
				// distCalcProc_BoundaryAndBoundary_3D within Radius.
				const Engine::Math::FixedVector3 apart{other.x - at.x, other.y - at.y, other.z - centerZ};
				const Fixed gap = Engine::Math::Length(apart) - (mine->sphereRadius + theirs.sphereRadius);
				if (gap > mine->radius)
					continue;
				++fellows;
				if (other.trueMember && !rubbedOff)
				{
					const Fixed dx = other.x - at.x, dy = other.y - at.y;
					rubbedOff = dx * dx + dy * dy <= rubOff;
				}
			}
			horde.trueMember = static_cast<std::int64_t>(fellows) >= static_cast<std::int64_t>(mine->count) - 1;
			horde.inHorde = horde.trueMember || rubbedOff;
			if (bonuses.empty())
				continue;
			// AIUpdateInterface::evaluateNationalismBonusClassic.
			WeaponBonusConditions &conditions = bonuses[row];
			if (catalog.hordeBonus != 0)
				SetWeaponBonus(conditions, catalog.hordeBonus, horde.inHorde, tick);
			const UpgradeMask &done = upgrades.Completed(player);
			if (mine->upgradeBonusAllowed && catalog.upgrade != HordeCatalog::NoUpgrade && done.Has(catalog.upgrade))
			{
				if (catalog.upgradeBonus != 0)
					SetWeaponBonus(conditions, catalog.upgradeBonus, true, tick);
				if (catalog.followerBonus != 0)
					SetWeaponBonus(conditions, catalog.followerBonus, catalog.followerUpgrade != HordeCatalog::NoUpgrade && done.Has(catalog.followerUpgrade), tick);
			}
			else if (catalog.upgradeBonus != 0)
				SetWeaponBonus(conditions, catalog.upgradeBonus, false, tick);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<engine::gameplay::HordeSystem>
{
	static constexpr std::string_view StableName = "engine.gameplay.horde";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
