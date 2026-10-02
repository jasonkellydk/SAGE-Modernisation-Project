export module games.renegade.session;
import std;
export import engine.ecs;
export import games.renegade.gameplay.defense.systems.defense_system;

export namespace renegade
{
// Execution ownership is confined to the game composition root. All entity
// data lives in the shared archetype world, in contiguous component columns.
class Session
{
public:
	explicit Session(std::size_t workers = 1, std::size_t chunkCapacity = 128) :
		m_world(ecs::WorldConfig{chunkCapacity})
	{
		m_world.RegisterComponent<engine::gameplay::Health>();
		m_world.RegisterComponent<engine::gameplay::Shield>();
		m_world.RegisterComponent<Defense>();
		m_world.FinalizeComponents();
		m_world.EmplaceResource<DamageRules>();
		m_world.EmplaceResource<DamageRequests>();
		m_world.EmplaceResource<DefenseHits>();
		m_registry.Register(m_defense);
		m_registry.Finalize(m_world.Components());
		m_scheduler = std::make_unique<ecs::Scheduler>(m_world, m_registry, engine::jobs::JobSystemConfig{workers});
		m_scheduler->Finalize(engine::time::FixedStep{60});
	}
	ecs::World &World() noexcept { return m_world; }
	const ecs::World &World() const noexcept { return m_world; }
	std::uint64_t Tick() const noexcept { return m_tick; }
	void Step()
	{
		m_scheduler->Execute(engine::time::SimulationTime{++m_tick, engine::time::FixedStep{60}});
		m_world.Resource<DamageRequests>().byTarget.clear();
	}
private:
	ecs::World m_world;
	DefenseSystem m_defense;
	ecs::SystemRegistry m_registry;
	std::unique_ptr<ecs::Scheduler> m_scheduler;
	std::uint64_t m_tick{0};
};
}
