export module engine.gameplay.common.spatial.algorithms.find_position;
import std;

export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;

// PartitionManager::findPositionAround: rings from minRadius out to
// maxRadius, 5 apart; on each, points from the start angle out to either
// side in turn (the inner ring only at the start angle; outer rings spaced
// (5 / (radius + 1)) of a sixth of a turn, as many as half a turn needs,
// rounded up), the first legal point wins. Legality is the caller's (the
// original's tryPosition: terrain, water, objects in the way).
export namespace engine::gameplay
{
inline constexpr std::int64_t FindPositionRingSpacing = 5;

template<typename Legal>
std::optional<Engine::Math::FixedVector2> FindPositionAround(Engine::Math::FixedVector2 center, Engine::Math::Fixed minRadius,
	Engine::Math::Fixed maxRadius, Engine::Math::TurnAngle startAngle, Legal &&legal)
{
	using Engine::Math::Fixed;
	using Engine::Math::TurnAngle;
	const Fixed spacing = Fixed::FromInt(FindPositionRingSpacing);
	const auto at = [&](Fixed radius, TurnAngle angle) { return center + Engine::Math::Direction(angle) * radius; };
	for (Fixed radius = minRadius; radius <= maxRadius; radius += spacing)
	{
		if (radius == minRadius)
		{
			if (const auto point = at(radius, startAngle); legal(point))
				return point;
			continue;
		}
		// angleSpacing = (5 / (dist + 1)) * (2 PI / 6): as a share of a turn, 5 / (6 (dist + 1)).
		const Fixed turns = Fixed::FromInt(FindPositionRingSpacing) / (Fixed::FromInt(6) * (radius + Fixed::One()));
		const auto step = TurnAngle{static_cast<std::uint32_t>((static_cast<std::uint64_t>(turns.Raw()) << 32) >> 16)};
		// samples = ceil((2 PI / angleSpacing) / 2)
		const std::int64_t samples = ((Fixed::One() / turns) / Fixed::FromInt(2)).Ceil();
		for (std::int64_t sample = 0; sample < samples; ++sample)
		{
			const TurnAngle offset{static_cast<std::uint32_t>(step.units * static_cast<std::uint64_t>(sample))};
			if (const auto point = at(radius, startAngle + offset); legal(point))
				return point;
			if (sample != 0)
				if (const auto point = at(radius, startAngle - offset); legal(point))
					return point;
		}
	}
	return std::nullopt;
}
}
