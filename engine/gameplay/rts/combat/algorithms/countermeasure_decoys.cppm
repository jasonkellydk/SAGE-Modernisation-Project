export module engine.gameplay.rts.combat.algorithms.countermeasure_decoys;
import std;

export import engine.gameplay.rts.combat.components.countermeasures;
export import engine.gameplay.common.spatial.components.transform;

// CountermeasuresBehavior::calculateCountermeasureToDivertTo: of its newest volley's flares still there (from the newest
// back, up to a volley's size of them), the one nearest the victim (2D, centres); none: none. The original stopped at
// the newest flare still there (a retail quirk: the community fix, taken here, weighs the whole volley).
export namespace engine::gameplay
{
template<typename Where>
ecs::Entity DivertTarget(const Countermeasures &decoys, Engine::Math::FixedVector2 victim, Where &&where)
{
	const std::uint32_t volley = std::max<std::uint32_t>(decoys.volleySize, 1u);
	std::uint32_t counted = 0;
	ecs::Entity best;
	Engine::Math::Fixed bestDistance;
	for (std::size_t index = decoys.flareCount; index-- > 0 && counted < volley;)
	{
		const Transform *at = where(decoys.flares[index]);
		if (at == nullptr)
			continue;
		const Engine::Math::Fixed distance = Engine::Math::DistanceSquared(at->position.XY(), victim);
		if (best == ecs::Entity{} || distance < bestDistance)
		{
			best = decoys.flares[index];
			bestDistance = distance;
		}
		++counted;
	}
	return best;
}
}
