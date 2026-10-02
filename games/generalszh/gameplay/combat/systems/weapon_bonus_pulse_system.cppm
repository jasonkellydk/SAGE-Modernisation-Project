export module games.generalszh.gameplay.combat.systems.weapon_bonus_pulse_system;
import std;

export import engine.ecs.system.system;
export import engine.ecs.system.chunk_outputs;
export import games.generalszh.gameplay.combat.components.weapon_bonus_pulse;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.team_member;
export import engine.gameplay.common.spatial.components.transform;

// WeaponBonusUpdate::update for everything that pulses, chunk-parallel: on its pulse tick it pulses (WeaponBonusPulses:
// ApplyWeaponBonusPulses gives the bonus about it) and sleeps BonusDelay.
export namespace generalszh::gameplay
{
// A pulse as it went out (its source may be gone by the time it lands: a one-tick marker).
struct WeaponBonusPulseEvent
{
	ecs::Entity source;
	Engine::Math::FixedVector2 centre;
	std::uint32_t definition{0};
	std::uint32_t player{0};
	std::uint32_t team{0xFFFFFFFFu};
	std::uint32_t reserved{0};
};

struct WeaponBonusPulses : ecs::ChunkOutputs<WeaponBonusPulseEvent>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::WeaponBonusPulses>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.weapon_bonus_pulses";
};
}

export namespace generalszh::gameplay
{
struct WeaponBonusPulseSystem
{
	using Query = ecs::Query<ecs::Write<WeaponBonusPulse>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Transform>,
		ecs::Read<engine::gameplay::Owner>, ecs::Optional<engine::gameplay::TeamMember>>;
	using Resources = ecs::Resources<ecs::Read<ObjectTemplates>, ecs::Write<WeaponBonusPulses>>;

	void BeforeChunks(Query &query, ecs::SystemContext &context) { context.Write<WeaponBonusPulses>().Reset(query.PreparedChunkCount()); }

	void Execute(Query::Chunk chunk, ecs::SystemContext &context) const
	{
		const ObjectTemplates &templates = context.Read<ObjectTemplates>();
		auto &out = context.Write<WeaponBonusPulses>().Slot(context);
		auto pulses = chunk.Get<WeaponBonusPulse>();
		const auto refs = chunk.Get<engine::gameplay::DefinitionRef>();
		const auto transforms = chunk.Get<engine::gameplay::Transform>();
		const auto owners = chunk.Get<engine::gameplay::Owner>();
		const auto members = chunk.Get<engine::gameplay::TeamMember>();
		const auto entities = chunk.Entities();
		const std::uint64_t now = context.Tick();
		for (std::size_t row = 0; row < pulses.size(); ++row)
		{
			const WeaponBonusPulseConfig *config = templates.WeaponBonusPulseOf(refs[row].index);
			if (config == nullptr || now < pulses[row].nextPulse)
				continue;
			out.push_back({entities[row], transforms[row].position.XY(), refs[row].index, owners[row].player, members.empty() ? 0xFFFFFFFFu : members[row].team});
			pulses[row].nextPulse = now + std::max<std::uint64_t>(config->delayTicks, 1);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::gameplay::WeaponBonusPulseSystem>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.weapon_bonus_pulse";
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
