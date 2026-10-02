export module games.renegade.gameplay.defense.resources.damage_rules;
import std;
export import engine.ecs.system.chunk_outputs;
export import engine.ecs.core.entity;
export import engine.ecs.core.resource_store;
export import Engine.Core.Math.Fixed;

export namespace renegade
{
using Fixed = Engine::Math::Fixed;
struct ArmorResponse { Fixed multiplier{Fixed::One()}; Fixed absorption{}; };
struct DamageRules
{
	std::map<std::pair<std::uint32_t, std::uint32_t>, ArmorResponse> responses;
	ArmorResponse At(std::uint32_t armor, std::uint32_t warhead) const
	{
		const auto found = responses.find({armor, warhead});
		return found != responses.end() ? found->second : ArmorResponse{};
	}
};
struct DamageRequest
{
	ecs::Entity target;
	ecs::Entity source;
	Fixed amount;
	std::uint32_t warhead{0};
	std::optional<std::uint32_t> alternateSkin;
	bool teammate{false};
	bool friendlyFire{false};
	// Set by mission composition only for attacks from the combat star.
	Fixed difficultyScale{Fixed::One()};
};
struct DamageRequests
{
	// Group by entity before the scheduler runs: O(hits + entities), no
	// every-hit scan per entity. Each entity's hit order is preserved.
	std::map<std::pair<std::uint32_t, std::uint32_t>, std::vector<DamageRequest>> byTarget;
	void Push(DamageRequest hit) { byTarget[{hit.target.index, hit.target.generation}].push_back(std::move(hit)); }
	std::span<const DamageRequest> For(ecs::Entity entity) const
	{
		const auto found = byTarget.find({entity.index, entity.generation});
		return found == byTarget.end() ? std::span<const DamageRequest>{} : found->second;
	}
};
struct DefenseHit
{
	ecs::Entity target;
	ecs::Entity source;
	Fixed healthDamage;
	Fixed shieldDamage;
	bool killed;
	bool repair;
};
struct DefenseHits : ecs::ChunkOutputs<DefenseHit> {};
}
export namespace ecs
{
template<> struct ResourceTraits<renegade::DamageRules> { static constexpr std::string_view StableName = "renegade.damage_rules"; };
template<> struct ResourceTraits<renegade::DamageRequests> { static constexpr std::string_view StableName = "renegade.damage_requests"; };
template<> struct ResourceTraits<renegade::DefenseHits> { static constexpr std::string_view StableName = "renegade.defense_hits"; };
}
