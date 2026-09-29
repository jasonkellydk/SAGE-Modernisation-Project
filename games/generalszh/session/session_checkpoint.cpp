module;
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <utility>
#include <vector>

module games.generalszh.session.session;

// Session checkpoints: the simulation state between ticks, in a fixed order.
// Per-tick channels (spatial index, shots, deaths, requests, casualties) are
// rebuilt or cleared by the next tick, so they are not part of it; level
// data (terrain, water, waypoints, authored teams) comes from the level.
namespace generalszh::session
{
namespace
{
constexpr std::uint32_t CheckpointMagic = 0x315A4853u; // "SHZ1"
}

void Session::SaveResources(engine::core::serialization::ByteWriter &writer) const
{
	writer.U64(m_tick);
	writer.U64(std::bit_cast<std::uint64_t>(m_random));
	m_templates.Save(writer);
	m_roster.Save(writer);
	m_names.Save(writer);
	m_manifest.Save(writer);
	m_relationships.Save(writer);
	m_shots.Save(writer);
	m_money.Save(writer);
	m_upgrades.Save(writer);
	m_sciences.Save(writer);
	m_outcome.Save(writer);
	m_aiPlayers.Save(writer);
	m_objectIds.Save(writer);
	m_ranks.Save(writer);
	m_sharedPowerTimers.Save(writer);
	m_bounties.Save(writer);
	m_world.Resource<gameplay::PlayerEnergy>().Save(writer);
	m_world.Resource<gameplay::ShroudMap>().Save(writer);
	m_scriptRecords.Save(writer);
	m_world.Resource<gameplay::AttackPriorities>().Save(writer);
	m_world.Resource<domain::AttackSquads>().Save(writer);
	m_world.Resource<domain::SoloPlay>().Save(writer);
	m_world.Resource<domain::ScoreKeepers>().Save(writer);
	m_commandBarOverrides.Save(writer);
	m_buildableOverrides.Save(writer);
	m_hulkLifetime.Save(writer);
	m_world.Resource<gameplay::AreaActivity>().Save(writer);
	m_world.Resource<gameplay::TemporaryWeaponFires>().Save(writer);
	m_world.Resource<domain::RetaliationModes>().Save(writer);
	m_world.Resource<domain::BattlePlanPlayers>().Save(writer);
	writer.U32(m_ground.ActiveBoundary());
	m_scripts->SaveState(writer);
}

bool Session::LoadResources(engine::core::serialization::ByteReader &reader)
{
	const auto tick = reader.U64();
	const auto random = reader.U64();
	if (!tick || !random)
		return false;
	m_tick = *tick;
	m_random = std::bit_cast<Engine::Math::RandomStream>(*random);
	return m_templates.Load(reader) && m_roster.Load(reader) && m_names.Load(reader) && m_manifest.Load(reader) &&
		m_relationships.Load(reader) && m_shots.Load(reader) && m_money.Load(reader) && m_upgrades.Load(reader) && m_sciences.Load(reader) && m_outcome.Load(reader) && m_aiPlayers.Load(reader) && m_objectIds.Load(reader) && m_ranks.Load(reader) && m_sharedPowerTimers.Load(reader) && m_bounties.Load(reader) && m_world.Resource<gameplay::PlayerEnergy>().Load(reader) && m_world.Resource<gameplay::ShroudMap>().Load(reader) && m_scriptRecords.Load(reader) && m_world.Resource<gameplay::AttackPriorities>().Load(reader) && m_world.Resource<domain::AttackSquads>().Load(reader) && m_world.Resource<domain::SoloPlay>().Load(reader) && m_world.Resource<domain::ScoreKeepers>().Load(reader) && m_commandBarOverrides.Load(reader) && m_buildableOverrides.Load(reader) && m_hulkLifetime.Load(reader) && m_world.Resource<gameplay::AreaActivity>().Load(reader) && m_world.Resource<gameplay::TemporaryWeaponFires>().Load(reader) && m_world.Resource<domain::RetaliationModes>().Load(reader) && m_world.Resource<domain::BattlePlanPlayers>().Load(reader) && [&] {
		const auto boundary = reader.U32();
		if (boundary)
			m_ground.SetActiveBoundary(*boundary);
		return boundary.has_value();
	}() && m_scripts->LoadState(reader);
}

void Session::SaveCheckpoint(engine::core::serialization::ByteWriter &writer) const
{
	writer.U32(CheckpointMagic);
	writer.U64(m_seed);
	SaveResources(writer);
	m_world.SaveCheckpoint(writer);
	m_navigation.SaveObstacles(writer);
}

std::vector<std::byte> Session::Checkpoint() const
{
	engine::core::serialization::ByteWriter writer;
	SaveCheckpoint(writer);
	return writer.Take();
}

bool Session::LoadCheckpoint(engine::core::serialization::ByteReader &reader)
{
	const auto magic = reader.U32();
	const auto seed = reader.U64();
	if (magic != CheckpointMagic || seed != m_seed)
		return false;
	if (!(LoadResources(reader) && m_world.LoadCheckpoint(reader)))
		return false;
	// The pathfinding grid: the terrain's, with the obstacle layer as it was (see NavigationGrid::SaveObstacles).
	domain::BuildNavigationGrid(m_game, m_navigation);
	// The bridges' decks as they were made (before any obstacle was stamped).
	domain::RegisterBridgeDecks(m_game);
	if (!(m_navigation.LoadObstacles(reader) && reader.AtEnd()))
		return false;
	for (gameplay::ClearancePlane &plane : m_navigation.Clearance())
		gameplay::BuildClearance(m_navigation, plane);
	return true;
}

ecs::StateHashValue Session::StateHash() const
{
	engine::core::serialization::ByteWriter resources;
	SaveResources(resources);
	ecs::StateHasher hasher;
	hasher.AppendU64(m_world.StateHash());
	hasher.AppendBytes(resources.Bytes());
	return hasher.Value();
}

std::unique_ptr<Session> Session::Restore(const engine::level::Level &level, const content::GameContent &content, SessionOptions options,
	std::span<const std::byte> checkpoint)
{
	std::unique_ptr<Session> session(new Session(level, content, std::move(options), false));
	engine::core::serialization::ByteReader reader(checkpoint);
	if (!session->LoadCheckpoint(reader))
		return nullptr;
	return session;
}
}
