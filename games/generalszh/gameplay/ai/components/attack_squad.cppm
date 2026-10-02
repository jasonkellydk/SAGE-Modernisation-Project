export module games.generalszh.gameplay.ai.components.attack_squad;
import std;

export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;
export import engine.ecs.system.chunk_outputs;
export import engine.core.serialization.byte_stream;
export import engine.ecs.core.entity_codec;
import engine.ecs.system.system;

// A unit attacking a team (AIUpdateInterface::aiAttackTeam: AIAttackSquadState): the squad it was sent against (an
// AttackSquads index: the team's members as the order was given, Squad::squadFromTeam) and whether a player gave the
// order (CMD_FROM_PLAYER picks as on hard). AttackSquads: those snapshots, and the script flag that makes every victim
// choice the normal one (ScriptEngine::setChooseVictimAlwaysUsesNormal). SquadsDone: the tick's units whose squad has
// nobody left to attack (the state succeeds: idle). Simulation state: checkpointed.
export namespace generalszh::gameplay
{
struct AttackSquad
{
	std::uint32_t squad{0};
	std::uint8_t fromPlayer{0};
	std::uint8_t reserved[3]{};
};

class AttackSquads
{
public:
	bool victimsAlwaysNormal{false};

	std::uint32_t Add(std::vector<ecs::Entity> members)
	{
		m_squads.push_back(std::move(members));
		return static_cast<std::uint32_t>(m_squads.size() - 1);
	}
	std::span<const ecs::Entity> Members(std::uint32_t squad) const
	{
		return squad < m_squads.size() ? std::span<const ecs::Entity>(m_squads[squad]) : std::span<const ecs::Entity>{};
	}
	// The squads nobody uses any more are emptied (their slots stay: indices never move).
	void Release(const std::vector<bool> &used)
	{
		for (std::size_t index = 0; index < m_squads.size(); ++index)
			if (index >= used.size() || !used[index])
				m_squads[index].clear();
	}
	std::size_t Size() const noexcept { return m_squads.size(); }

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.Flag(victimsAlwaysNormal);
		writer.U32(static_cast<std::uint32_t>(m_squads.size()));
		for (const auto &squad : m_squads)
		{
			writer.U32(static_cast<std::uint32_t>(squad.size()));
			for (const ecs::Entity member : squad)
				ecs::WriteEntity(writer, member);
		}
	}
	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto flag = reader.Flag();
		const auto count = reader.U32();
		if (!flag || !count)
			return false;
		std::vector<std::vector<ecs::Entity>> squads(*count);
		for (auto &squad : squads)
		{
			const auto size = reader.U32();
			for (std::uint32_t index = 0; size && index < *size && !reader.Failed(); ++index)
				squad.push_back(ecs::ReadEntity(reader).value_or(ecs::Entity{}));
		}
		if (reader.Failed())
			return false;
		victimsAlwaysNormal = *flag;
		m_squads = std::move(squads);
		return true;
	}

private:
	std::vector<std::vector<ecs::Entity>> m_squads;
};

struct SquadsDone : ecs::ChunkOutputs<ecs::Entity>
{
};
}

export namespace ecs
{
template<>
struct ComponentTraits<generalszh::gameplay::AttackSquad>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.attack_squad";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};

template<>
struct ResourceTraits<generalszh::gameplay::AttackSquads>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.attack_squads";
};

template<>
struct ResourceTraits<generalszh::gameplay::SquadsDone>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.squads_done";
};
}
