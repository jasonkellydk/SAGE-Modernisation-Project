module;
#include <bit>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

module games.generalszh.session.session;

import games.generalszh.gameplay.orders.resources.hotkey_squads;
import engine.gameplay.rts.movement.resources.path_points;

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

void Session::SaveResources(engine::core::serialization::ByteWriter &writer, std::vector<std::pair<std::string, std::size_t>> *marks) const
{
	writer.U64(m_tick);
	if (marks != nullptr)
		marks->emplace_back("m_tick", writer.Bytes().size());
	writer.U64(std::bit_cast<std::uint64_t>(m_random));
	if (marks != nullptr)
		marks->emplace_back("m_random", writer.Bytes().size());
	m_templates.Save(writer);
	if (marks != nullptr)
		marks->emplace_back("m_templates", writer.Bytes().size());
	m_roster.Save(writer);
	if (marks != nullptr)
		marks->emplace_back("m_roster", writer.Bytes().size());
	m_names.Save(writer);
	if (marks != nullptr)
		marks->emplace_back("m_names", writer.Bytes().size());
	m_manifest.Save(writer);
	if (marks != nullptr)
		marks->emplace_back("m_manifest", writer.Bytes().size());
	m_relationships.Save(writer);
	if (marks != nullptr)
		marks->emplace_back("m_relationships", writer.Bytes().size());
	m_shots.Save(writer);
	if (marks != nullptr)
		marks->emplace_back("m_shots", writer.Bytes().size());
	m_money.Save(writer);
	if (marks != nullptr)
		marks->emplace_back("m_money", writer.Bytes().size());
	m_upgrades.Save(writer);
	if (marks != nullptr)
		marks->emplace_back("m_upgrades", writer.Bytes().size());
	m_sciences.Save(writer);
	if (marks != nullptr)
		marks->emplace_back("m_sciences", writer.Bytes().size());
	m_outcome.Save(writer);
	if (marks != nullptr)
		marks->emplace_back("m_outcome", writer.Bytes().size());
	m_aiPlayers.Save(writer);
	if (marks != nullptr)
		marks->emplace_back("m_aiPlayers", writer.Bytes().size());
	m_objectIds.Save(writer);
	if (marks != nullptr)
		marks->emplace_back("m_objectIds", writer.Bytes().size());
	m_ranks.Save(writer);
	if (marks != nullptr)
		marks->emplace_back("m_ranks", writer.Bytes().size());
	m_sharedPowerTimers.Save(writer);
	if (marks != nullptr)
		marks->emplace_back("m_sharedPowerTimers", writer.Bytes().size());
	m_bounties.Save(writer);
	if (marks != nullptr)
		marks->emplace_back("m_bounties", writer.Bytes().size());
	m_world.Resource<gameplay::PlayerEnergy>().Save(writer);
	if (marks != nullptr)
		marks->emplace_back("PlayerEnergy", writer.Bytes().size());
	m_world.Resource<gameplay::ShroudMap>().Save(writer);
	if (marks != nullptr)
		marks->emplace_back("ShroudMap", writer.Bytes().size());
	m_scriptRecords.Save(writer);
	if (marks != nullptr)
		marks->emplace_back("m_scriptRecords", writer.Bytes().size());
	m_world.Resource<gameplay::AttackPriorities>().Save(writer);
	if (marks != nullptr)
		marks->emplace_back("AttackPriorities", writer.Bytes().size());
	m_world.Resource<domain::AttackSquads>().Save(writer);
	if (marks != nullptr)
		marks->emplace_back("AttackSquads", writer.Bytes().size());
	m_world.Resource<domain::SoloPlay>().Save(writer);
	if (marks != nullptr)
		marks->emplace_back("SoloPlay", writer.Bytes().size());
	m_world.Resource<domain::ScoreKeepers>().Save(writer);
	if (marks != nullptr)
		marks->emplace_back("ScoreKeepers", writer.Bytes().size());
	m_commandBarOverrides.Save(writer);
	if (marks != nullptr)
		marks->emplace_back("m_commandBarOverrides", writer.Bytes().size());
	m_buildableOverrides.Save(writer);
	if (marks != nullptr)
		marks->emplace_back("m_buildableOverrides", writer.Bytes().size());
	m_hulkLifetime.Save(writer);
	if (marks != nullptr)
		marks->emplace_back("m_hulkLifetime", writer.Bytes().size());
	m_world.Resource<gameplay::AreaActivity>().Save(writer);
	if (marks != nullptr)
		marks->emplace_back("AreaActivity", writer.Bytes().size());
	m_world.Resource<gameplay::TemporaryWeaponFires>().Save(writer);
	if (marks != nullptr)
		marks->emplace_back("TemporaryWeaponFires", writer.Bytes().size());
	m_world.Resource<gameplay::HistoricDamage>().Save(writer);
	if (marks != nullptr)
		marks->emplace_back("HistoricDamage", writer.Bytes().size());
	m_world.Resource<domain::RetaliationModes>().Save(writer);
	if (marks != nullptr)
		marks->emplace_back("RetaliationModes", writer.Bytes().size());
	m_world.Resource<domain::BattlePlanPlayers>().Save(writer);
	if (marks != nullptr)
		marks->emplace_back("BattlePlanPlayers", writer.Bytes().size());
	m_world.Resource<domain::DeferredOrders>().Save(writer);
	if (marks != nullptr)
		marks->emplace_back("DeferredOrders", writer.Bytes().size());
	m_world.Resource<domain::WaterChanges>().Save(writer);
	if (marks != nullptr)
		marks->emplace_back("WaterChanges", writer.Bytes().size());
	m_world.Resource<domain::MusicProgress>().Save(writer);
	if (marks != nullptr)
		marks->emplace_back("MusicProgress", writer.Bytes().size());
	m_world.Resource<domain::HotkeySquads>().Save(writer);
	if (marks != nullptr)
		marks->emplace_back("HotkeySquads", writer.Bytes().size());
	m_world.Resource<domain::MapSceneryRules>().Save(writer);
	if (marks != nullptr)
		marks->emplace_back("MapSceneryRules", writer.Bytes().size());
	m_world.Resource<domain::SceneryClearings>().Save(writer);
	if (marks != nullptr)
		marks->emplace_back("SceneryClearings", writer.Bytes().size());
	m_world.Resource<gameplay::GoalCells>().Save(writer);
	if (marks != nullptr)
		marks->emplace_back("GoalCells", writer.Bytes().size());
	m_world.Resource<domain::TeamWaypoints>().Save(writer);
	if (marks != nullptr)
		marks->emplace_back("TeamWaypoints", writer.Bytes().size());
	m_world.Resource<gameplay::PathPoints>().Save(writer);
	if (marks != nullptr)
		marks->emplace_back("PathPoints", writer.Bytes().size());
	m_academy.Save(writer);
	if (marks != nullptr)
		marks->emplace_back("m_academy", writer.Bytes().size());
	m_ground.SaveWater(writer);
	if (marks != nullptr)
		marks->emplace_back("m_ground", writer.Bytes().size());
	writer.U32(m_ground.ActiveBoundary());
	if (marks != nullptr)
		marks->emplace_back("m_ground.boundary", writer.Bytes().size());
	m_scripts->SaveState(writer);
	if (marks != nullptr)
		marks->emplace_back("m_scripts", writer.Bytes().size());
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
		m_relationships.Load(reader) && m_shots.Load(reader) && m_money.Load(reader) && m_upgrades.Load(reader) && m_sciences.Load(reader) && m_outcome.Load(reader) && m_aiPlayers.Load(reader) && m_objectIds.Load(reader) && m_ranks.Load(reader) && m_sharedPowerTimers.Load(reader) && m_bounties.Load(reader) && m_world.Resource<gameplay::PlayerEnergy>().Load(reader) && m_world.Resource<gameplay::ShroudMap>().Load(reader) && m_scriptRecords.Load(reader) && m_world.Resource<gameplay::AttackPriorities>().Load(reader) && m_world.Resource<domain::AttackSquads>().Load(reader) && m_world.Resource<domain::SoloPlay>().Load(reader) && m_world.Resource<domain::ScoreKeepers>().Load(reader) && m_commandBarOverrides.Load(reader) && m_buildableOverrides.Load(reader) && m_hulkLifetime.Load(reader) && m_world.Resource<gameplay::AreaActivity>().Load(reader) && m_world.Resource<gameplay::TemporaryWeaponFires>().Load(reader) && m_world.Resource<gameplay::HistoricDamage>().Load(reader) && m_world.Resource<domain::RetaliationModes>().Load(reader) && m_world.Resource<domain::BattlePlanPlayers>().Load(reader) && m_world.Resource<domain::DeferredOrders>().Load(reader) && m_world.Resource<domain::WaterChanges>().Load(reader) && m_world.Resource<domain::MusicProgress>().Load(reader) && m_world.Resource<domain::HotkeySquads>().Load(reader) && m_world.Resource<domain::MapSceneryRules>().Load(reader) && m_world.Resource<domain::SceneryClearings>().Load(reader) && m_world.Resource<gameplay::GoalCells>().Load(reader) && m_world.Resource<domain::TeamWaypoints>().Load(reader) && m_world.Resource<gameplay::PathPoints>().Load(reader) && m_academy.Load(reader) && m_ground.LoadWater(reader) && [&] {
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
	domain::RegisterLayers(m_game);
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

std::vector<std::pair<std::string, ecs::StateHashValue>> Session::ResourceHashes() const
{
	engine::core::serialization::ByteWriter writer;
	std::vector<std::pair<std::string, std::size_t>> marks;
	SaveResources(writer, &marks);
	const std::span<const std::byte> bytes = writer.Bytes();
	std::vector<std::pair<std::string, ecs::StateHashValue>> hashes;
	std::size_t from = 0;
	for (const auto &[name, to] : marks)
	{
		ecs::StateHasher hasher;
		hasher.AppendBytes(bytes.subspan(from, to - from));
		hashes.emplace_back(name, hasher.Value());
		from = to;
	}
	return hashes;
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
