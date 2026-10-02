export module games.generalszh.gameplay.combat.systems.cooldown_creation_system;
import std;

export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import games.generalszh.gameplay.combat.components.cooldown_creation;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.weapons.components.armament;
export import engine.gameplay.common.weapons.components.weapon_slots;
export import engine.gameplay.common.weapons.resources.weapon_catalog;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.rts.upgrades.components.upgradable;
export import engine.gameplay.rts.upgrades.resources.player_upgrades;

// FireOCLAfterWeaponCooldownUpdate::update, every tick, chunk-parallel, before this tick's shots: each module watches
// its weapon slot while that is the current weapon and its upgrade conditions hold. A shot last tick counts (the
// first marks when firing began); a weapon that could have fired and did not, after at least MinShotsToCreateOCL in
// a row, has stopped - its creation list goes, lasting OCLLifetimePerSecond ms for each second of firing (capped;
// none: the list's own lifetime). Leaving the slot (switching weapons) after enough shots fires it too, unless it is
// the upgrade conditions that ended. Any change of watching starts the count again. The lists are made after the
// tick (CooldownCreationEvents).
export namespace generalszh::gameplay
{
struct CooldownCreationEvent
{
	ecs::Entity entity;
	std::uint32_t creation{0xFFFFFFFFu};
	std::uint64_t lifetimeTicks{0}; // 0: no override
};

struct CooldownCreationEvents : ecs::ChunkOutputs<CooldownCreationEvent>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::CooldownCreationEvents>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.cooldown_creation_events";
};
}

export namespace generalszh::gameplay
{
struct CooldownCreationSystem
{
	using Query = ecs::Query<ecs::Write<CooldownCreations>, ecs::Read<engine::gameplay::Armament>, ecs::Read<engine::gameplay::DefinitionRef>,
		ecs::Read<engine::gameplay::Owner>, ecs::Optional<engine::gameplay::WeaponSlots>, ecs::Optional<engine::gameplay::Upgradable>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Read<engine::gameplay::PlayerUpgrades>, ecs::Write<CooldownCreationEvents>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) const { context.Write<CooldownCreationEvents>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		namespace gp = engine::gameplay;
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		const gp::PlayerUpgrades &players = context.Read<gp::PlayerUpgrades>();
		auto &out = context.Write<CooldownCreationEvents>().Slot(context);
		const std::uint64_t now = context.Tick();
		auto states = chunk.Get<CooldownCreations>();
		const auto armaments = chunk.Get<gp::Armament>();
		const auto definitions = chunk.Get<gp::DefinitionRef>();
		const auto owners = chunk.Get<gp::Owner>();
		const auto slotSets = chunk.Get<gp::WeaponSlots>();
		const auto upgradables = chunk.Get<gp::Upgradable>();
		const auto entities = chunk.Entities();
		for (std::size_t row = 0; row < states.size(); ++row)
		{
			const auto modules = templates.cooldownCreations.Of(definitions[row].index);
			const gp::Armament &armament = armaments[row];
			const bool armed = armament.weapon != gp::WeaponCatalog::None;
			const std::uint32_t current = slotSets.empty() ? 0u : slotSets[row].current;
			gp::UpgradeMask key = players.Completed(owners[row].player);
			if (!upgradables.empty())
				key.Add(upgradables[row].completed);
			for (std::size_t index = 0; index < modules.size() && index < CooldownCreationCatalog::MaxModules; ++index)
			{
				const CooldownCreationConfig &module = modules[index];
				CooldownCreationState &state = states[row].modules[index];
				const auto fire = [&] {
					const std::uint64_t seconds = (now - state.startTick) * module.lifetimePerSecond / 1000;
					out.push_back({entities[row], module.creation, std::min<std::uint64_t>(seconds, module.maxTicks)});
					state.shots = 0;
					state.startTick = 0;
				};
				bool valid = armed && current == module.slot;
				bool mayFire = true;
				if (valid && !module.Active(key))
				{
					valid = false;
					mayFire = false;
				}
				if (valid)
				{
					if (now > 0 && armament.firedTick == now - 1)
					{
						if (++state.shots == 1)
							state.startTick = now;
					}
					else if (armament.readyTick < now && module.minShots <= state.shots)
						fire();
				}
				else if (mayFire)
				{
					const bool slotArmed = slotSets.empty() ? module.slot == 0 && armed : slotSets[row].slots[module.slot].weapon != gp::WeaponCatalog::None;
					if (slotArmed && module.minShots <= state.shots)
						fire();
				}
				if ((valid ? 1u : 0u) != state.valid)
				{
					state.valid = valid ? 1u : 0u;
					state.shots = 0;
					state.startTick = 0;
				}
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::CooldownCreationSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.cooldown_creation";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
