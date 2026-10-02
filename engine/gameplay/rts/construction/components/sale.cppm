export module engine.gameplay.rts.construction.components.sale;
import std;

export import engine.ecs.core.component_registry;

// A structure being sold (the original's ObjectSellInfo and OBJECT_STATUS_SOLD):
// since which tick, and whether its construction has run out (it shows as
// SOLD while its scaffold sinks away).
export namespace engine::gameplay
{
struct Sale
{
	std::uint64_t since{0};
	std::uint8_t sunk{0};
	std::uint8_t reserved[7]{};
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Sale>
{
	static constexpr std::string_view StableName = "engine.gameplay.sale";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
