export module engine.gameplay.common.spatial.components.affine_pose;
import std;
export import engine.ecs.core.component_registry;
export import Engine.Core.Math.FixedAffineTransform3;

export namespace engine::gameplay
{
// Full authored basis for scene objects that cannot be represented by yaw
// alone (rotated/scaled props, doors, elevators and building aggregates).
struct AffinePose { Engine::Math::FixedAffineTransform3 transform; };
}
export namespace ecs
{
template<> struct ComponentTraits<engine::gameplay::AffinePose>
{
	static constexpr std::string_view StableName = "engine.gameplay.affine_pose";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
	static void HashState(const engine::gameplay::AffinePose &value, StateHasher &hasher) noexcept {
		for (const auto element : value.transform.elements) hasher.AppendU64(static_cast<std::uint64_t>(element.Raw()));
	}
	static void Save(const engine::gameplay::AffinePose &value, engine::core::serialization::ByteWriter &writer) {
		for (const auto element : value.transform.elements) writer.U64(static_cast<std::uint64_t>(element.Raw()));
	}
	static bool Load(engine::gameplay::AffinePose &value, engine::core::serialization::ByteReader &reader) {
		for (auto &element : value.transform.elements) {
			const auto bits = reader.U64(); if (!bits) return false;
			element = Engine::Math::Fixed::FromRaw(std::bit_cast<std::int64_t>(*bits));
		}
		return true;
	}
};
}
