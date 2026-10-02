export module games.generalszh.gameplay.orders.resources.hotkey_squads;
import std;

export import engine.ecs.core.entity;
export import engine.core.serialization.byte_stream;
export import engine.ecs.core.entity_codec;
import engine.ecs.system.system;

// Each player's hotkey squads (Player::m_squads, NUM_HOTKEY_SQUADS 10; Squad.cpp): the objects a player put in its
// control groups 0..9 (MSG_CREATE_TEAMn), in the order given. A squad keeps an object until the object is gone
// (Squad::getAllObjects prunes ids no longer found) or it is put in another of its player's squads. The presentation
// reads them to select a group (its live, selectable members) and to number the selected units. Simulation state:
// checkpointed (Player::xfer's squads).
export namespace generalszh::gameplay
{
inline constexpr std::int32_t HotkeySquadCount = 10; // NUM_HOTKEY_SQUADS
inline constexpr std::int32_t NoHotkeySquad = -1;    // NO_HOTKEY_SQUAD

struct HotkeySquads
{
	// By player, then by squad number.
	std::vector<std::array<std::vector<ecs::Entity>, HotkeySquadCount>> players;

	std::span<const ecs::Entity> Members(std::uint32_t player, std::int32_t squad) const noexcept
	{
		if (player >= players.size() || squad < 0 || squad >= HotkeySquadCount)
			return {};
		return players[player][static_cast<std::size_t>(squad)];
	}

	void Save(engine::core::serialization::ByteWriter &writer) const
	{
		writer.U32(static_cast<std::uint32_t>(players.size()));
		for (const auto &squads : players)
			for (const auto &squad : squads)
			{
				writer.U32(static_cast<std::uint32_t>(squad.size()));
				for (const ecs::Entity member : squad)
					ecs::WriteEntity(writer, member);
			}
	}

	bool Load(engine::core::serialization::ByteReader &reader)
	{
		const auto count = reader.U32();
		if (!count || *count > 4096)
			return false;
		std::vector<std::array<std::vector<ecs::Entity>, HotkeySquadCount>> loaded(*count);
		for (auto &squads : loaded)
			for (auto &squad : squads)
			{
				const auto size = reader.U32();
				for (std::uint32_t index = 0; size && index < *size && !reader.Failed(); ++index)
					squad.push_back(ecs::ReadEntity(reader).value_or(ecs::Entity{}));
			}
		if (reader.Failed())
			return false;
		players = std::move(loaded);
		return true;
	}
};

// Player::processCreateTeamGameMessage: an invalid number does nothing; else the squad is emptied, then each object of
// the message that still exists (`exists`: GameLogic::findObjectByID) leaves every squad of the player
// (removeObjectFromHotkeySquad) and joins this one, in the message's order. Whose objects they are is not checked.
template<typename Exists>
void CreateHotkeySquad(HotkeySquads &squads, std::uint32_t player, std::int32_t squad, std::span<const ecs::Entity> units, Exists &&exists)
{
	if (squad < 0 || squad >= HotkeySquadCount)
		return;
	if (squads.players.size() <= player)
		squads.players.resize(static_cast<std::size_t>(player) + 1);
	auto &mine = squads.players[player];
	mine[static_cast<std::size_t>(squad)].clear();
	for (const ecs::Entity unit : units)
	{
		if (!exists(unit))
			continue;
		for (auto &other : mine)
			if (const auto found = std::find(other.begin(), other.end(), unit); found != other.end())
				other.erase(found); // Squad::removeObject: the first occurrence
		mine[static_cast<std::size_t>(squad)].push_back(unit);
	}
}

// Player::getSquadNumberForObject: the first of the player's squads holding the object, else NO_HOTKEY_SQUAD.
inline std::int32_t SquadNumberOf(const HotkeySquads &squads, std::uint32_t player, ecs::Entity unit) noexcept
{
	if (player >= squads.players.size())
		return NoHotkeySquad;
	for (std::int32_t squad = 0; squad < HotkeySquadCount; ++squad)
	{
		const auto &members = squads.players[player][static_cast<std::size_t>(squad)];
		if (std::find(members.begin(), members.end(), unit) != members.end())
			return squad;
	}
	return NoHotkeySquad;
}
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::HotkeySquads>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.hotkey_squads";
};
}
