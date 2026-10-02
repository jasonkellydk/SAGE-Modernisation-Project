export module engine.gameplay.common.health.components.shield;
import std;
export import engine.ecs.core.component_registry;
export import Engine.Core.Math.Fixed;

export namespace engine::gameplay
{
// An independent protective reservoir. Games decide how hits and repairs
// route to it; no game's armor vocabulary or damage rules live here.
struct Shield
{
	Engine::Math::Fixed current;
	Engine::Math::Fixed maximum;
	std::uint32_t armor{0};
	std::uint32_t reserved{0};
};
}
export namespace ecs
{
template<> struct ComponentTraits<engine::gameplay::Shield>
{
	static constexpr std::string_view StableName = "engine.gameplay.shield";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::Shield &value, StateHasher &hasher) noexcept
	{
		hasher.AppendU64(static_cast<std::uint64_t>(value.current.Raw()));
		hasher.AppendU64(static_cast<std::uint64_t>(value.maximum.Raw()));
		hasher.AppendU64(value.armor);
	}
};
}
