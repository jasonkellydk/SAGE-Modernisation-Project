export module engine.gameplay.common.areas.components.volume_contact;
import std;
export import engine.ecs.core.component_registry;
export import engine.ecs.core.entity;

export namespace engine::gameplay
{
// One relationship per volume/probe pair, in archetype component columns.
// There is no fixed per-player limit on simultaneous volume memberships.
struct VolumeContact
{
	std::uint32_t volume{};
	ecs::Entity probe;
	std::uint32_t inside{};
	std::uint64_t entered_tick{}, order{};
};
}
export namespace ecs
{
template<> struct ComponentTraits<engine::gameplay::VolumeContact>
{
	static constexpr std::string_view StableName = "engine.gameplay.volume_contact";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::VolumeContact &value, StateHasher &hasher) noexcept {
		hasher.AppendU64(value.volume); hasher.AppendU64(value.probe.index); hasher.AppendU64(value.probe.generation);
		hasher.AppendU64(value.inside); hasher.AppendU64(value.entered_tick); hasher.AppendU64(value.order);
	}
	static void Save(const engine::gameplay::VolumeContact &value, engine::core::serialization::ByteWriter &writer) {
		writer.U32(value.volume); writer.U32(value.probe.index); writer.U32(value.probe.generation);
		writer.U32(value.inside); writer.U64(value.entered_tick); writer.U64(value.order);
	}
	static bool Load(engine::gameplay::VolumeContact &value, engine::core::serialization::ByteReader &reader) {
		const auto volume = reader.U32(), index = reader.U32(), generation = reader.U32(), inside = reader.U32();
		const auto tick = reader.U64(), order = reader.U64();
		if (!volume || !index || !generation || !inside || *inside > 1 || !tick || !order) return false;
		value = {*volume, {*index, *generation}, *inside, *tick, *order}; return true;
	}
};
}
