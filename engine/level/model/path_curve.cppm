export module engine.level.model.path_curve;
import std;
export import Engine.Core.Math.FixedVector;

export namespace engine::level
{
// Immutable cubic route geometry. Simulation owns a parameter and a target,
// while this level data is shared across actors. Columns keep key and tangent
// storage independent of file schemas and per-entity ECS state.
struct PathCurve3
{
	std::vector<Engine::Math::Fixed> keys;
	std::vector<Engine::Math::FixedVector3> positions, incoming, outgoing;
	Engine::Math::Fixed length;

	bool IsValid() const noexcept {
		if (keys.empty() || positions.size() != keys.size() || incoming.size() != keys.size() || outgoing.size() != keys.size()) return false;
		for (std::size_t i = 1; i < keys.size(); ++i) if (keys[i] <= keys[i - 1]) return false;
		return length >= Engine::Math::Fixed{};
	}
	Engine::Math::FixedVector3 Sample(Engine::Math::Fixed parameter) const {
		using namespace Engine::Math;
		if (!IsValid()) throw std::invalid_argument("invalid path curve columns or keys");
		if (parameter <= keys.front()) return positions.front();
		if (parameter >= keys.back()) return positions.back();
		const auto upper = std::size_t(std::upper_bound(keys.begin(), keys.end(), parameter) - keys.begin());
		const auto lower = upper - 1;
		const auto t = (parameter - keys[lower]) / (keys[upper] - keys[lower]);
		const auto square = t * t, cube = square * t;
		const auto h0 = Fixed::FromInt(2) * cube - Fixed::FromInt(3) * square + Fixed::One();
		const auto h1 = -Fixed::FromInt(2) * cube + Fixed::FromInt(3) * square;
		const auto h2 = cube - Fixed::FromInt(2) * square + t, h3 = cube - square;
		return positions[lower] * h0 + positions[upper] * h1 + outgoing[lower] * h2 + incoming[upper] * h3;
	}
};

inline std::expected<PathCurve3, std::string> BuildCardinalPathCurve3(
	std::span<const Engine::Math::FixedVector3> points, Engine::Math::Fixed tightness)
{
	using namespace Engine::Math;
	if (points.empty() || tightness < Fixed{} || tightness > Fixed::One()) return std::unexpected("invalid cardinal path points or tightness");
	PathCurve3 curve;
	for (const auto point : points) {
		if (!curve.positions.empty() && point == curve.positions.back()) continue;
		if (!curve.positions.empty()) curve.length += Length(point - curve.positions.back());
		curve.keys.push_back(curve.length); curve.positions.push_back(point);
	}
	curve.incoming.resize(curve.keys.size()); curve.outgoing.resize(curve.keys.size());
	if (curve.keys.size() == 1) return curve;
	for (auto &key : curve.keys) key /= curve.length;
	curve.keys.back() = Fixed::One();
	if (!curve.IsValid()) return std::unexpected("cardinal path segments are below parameter precision");
	const auto last = curve.keys.size() - 1;
	const auto scale = Fixed::One() - tightness;
	const auto endpoint_time = curve.keys[1] + curve.keys[last] - curve.keys[last - 1];
	curve.outgoing[0] = (curve.positions[1] - curve.positions[0]) * scale * (Fixed::FromInt(2) * curve.keys[1] / endpoint_time);
	curve.incoming[last] = (curve.positions[last] - curve.positions[last - 1]) * scale *
		(Fixed::FromInt(2) * (curve.keys[last] - curve.keys[last - 1]) / endpoint_time);
	for (std::size_t i = 1; i < last; ++i) {
		const auto tangent = (curve.positions[i + 1] - curve.positions[i - 1]) * scale;
		const auto time = curve.keys[i + 1] - curve.keys[i - 1];
		curve.incoming[i] = tangent * (Fixed::FromInt(2) * (curve.keys[i] - curve.keys[i - 1]) / time);
		curve.outgoing[i] = tangent * (Fixed::FromInt(2) * (curve.keys[i + 1] - curve.keys[i]) / time);
	}
	return curve;
}
}
