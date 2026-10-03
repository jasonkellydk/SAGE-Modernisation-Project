export module engine.level.collision.collision_scene;
import std;
export import Engine.Core.Math.FixedVector;

export namespace engine::level
{
using Engine::Math::Fixed;
using Engine::Math::FixedVector3;
struct CollisionBox3 {FixedVector3 center, extent;};
struct CollisionHit3
{
	Fixed fraction{Fixed::One()};
	FixedVector3 normal;
	std::uint64_t subject{};
	std::uint32_t triangle{}, surface{};
	bool starts_overlapping{};
};
}
namespace engine::level::collision_detail
{
using namespace Engine::Math;
struct Bounds
{
	FixedVector3 minimum, maximum;
	void Include(FixedVector3 p) {
		minimum = {std::min(minimum.x, p.x), std::min(minimum.y, p.y), std::min(minimum.z, p.z)};
		maximum = {std::max(maximum.x, p.x), std::max(maximum.y, p.y), std::max(maximum.z, p.z)};
	}
	bool Overlaps(const Bounds &b) const {
		return minimum.x <= b.maximum.x && maximum.x >= b.minimum.x && minimum.y <= b.maximum.y && maximum.y >= b.minimum.y && minimum.z <= b.maximum.z && maximum.z >= b.minimum.z;
	}
};
inline std::optional<CollisionHit3> Sweep(FixedVector3 a, FixedVector3 b, FixedVector3 c, CollisionBox3 box, FixedVector3 move)
{
	const std::array<FixedVector3, 3> vertices{a - box.center, b - box.center, c - box.center};
	const std::array<FixedVector3, 3> basis{{{Fixed::One(), {}, {}}, {{}, Fixed::One(), {}}, {{}, {}, Fixed::One()}}};
	const std::array<FixedVector3, 3> edges{b - a, c - b, a - c};
	std::array<FixedVector3, 13> axes; std::copy(basis.begin(), basis.end(), axes.begin());
	axes[3] = Cross(b - a, c - a); unsigned next = 4;
	for (const auto edge : edges) for (const auto axis : basis) axes[next++] = Cross(edge, axis);
	Fixed entry = Fixed::Min(), exit = Fixed::Max(); FixedVector3 normal;
	bool interior = true;
	for (auto axis : axes) {
		const auto scale = std::max({Abs(axis.x), Abs(axis.y), Abs(axis.z)}); if (scale == Fixed{}) continue;
		axis = axis / scale; // Keep SAT projections in range without losing small axes to squared-length rounding.
		const auto first = Dot(vertices[0], axis), second = Dot(vertices[1], axis), third = Dot(vertices[2], axis);
		const auto low = std::min({first, second, third}), high = std::max({first, second, third});
		const auto radius = box.extent.x * Abs(axis.x) + box.extent.y * Abs(axis.y) + box.extent.z * Abs(axis.z);
		const bool overlap = low <= radius && high >= -radius;
		interior &= low < radius && high > -radius;
		const auto speed = Dot(move, axis);
		if (speed == Fixed{}) {if (!overlap) return {}; continue;}
		const auto begin = speed > Fixed{} ? (low - radius) / speed : (high + radius) / speed;
		const auto end = speed > Fixed{} ? (high + radius) / speed : (low - radius) / speed;
		if (begin > entry) {entry = begin; normal = speed > Fixed{} ? -axis : axis;}
		exit = std::min(exit, end); if (entry > exit) return {};
	}
	if (interior) return CollisionHit3{{}, Normalize(Cross(b - a, c - a)), 0, 0, 0, true};
	// A boundary contact blocks approaching movement, while tangential motion
	// and movement away from that contact stay free.
	if (exit < Fixed{} || entry < Fixed{} || entry > Fixed::One()) return {};
	return CollisionHit3{entry, Normalize(normal)};
}
}

export namespace engine::level
{
// Immutable authored collision geometry. Triangle positions and metadata are
// independent columns; a deterministic BVH is prepared once, then queried by
// parallel ECS systems without allocation or mutable per-actor controllers.
class CollisionScene3
{
public:
	void AddLayer(std::shared_ptr<const CollisionScene3> layer) {
		if(m_ready) throw std::logic_error("collision scene is immutable after preparation");
		if(!layer || !layer->m_ready) throw std::invalid_argument("collision layer must be a prepared immutable scene");
		m_layers.push_back(std::move(layer));
	}
	void AddTriangle(FixedVector3 first, FixedVector3 second, FixedVector3 third,
		std::uint64_t subject, std::uint32_t categories, std::uint32_t surface = 0)
	{
		if (m_ready) throw std::logic_error("collision scene is immutable after preparation");
		if (m_first.size() >= std::numeric_limits<std::uint32_t>::max()) throw std::length_error("collision triangle index width exceeded");
		if (Engine::Math::Cross(second - first, third - first) == FixedVector3{}) return;
		m_first.push_back(first); m_second.push_back(second); m_third.push_back(third);
		m_subjects.push_back(subject); m_categories.push_back(categories); m_surfaces.push_back(surface);
	}
	void Prepare()
	{
		if (m_ready) return;
		m_order.resize(m_first.size()); std::iota(m_order.begin(), m_order.end(), 0u);
		if (!m_order.empty()) Build(0, static_cast<std::uint32_t>(m_order.size())); m_ready = true;
	}
	std::size_t TriangleCount() const noexcept {auto count=m_first.size();for(const auto& layer:m_layers) count+=layer->TriangleCount();return count;}
	std::optional<CollisionHit3> Cast(CollisionBox3 box, FixedVector3 move, std::uint32_t categories,
		std::optional<std::uint64_t> ignore = {}) const
	{
		if (!m_ready) throw std::logic_error("collision scene must be prepared before queries");
		if (box.extent.x < Fixed{} || box.extent.y < Fixed{} || box.extent.z < Fixed{}) throw std::invalid_argument("negative collision box extent");
		if (!categories) return {};
		collision_detail::Bounds bounds{box.center - box.extent, box.center + box.extent};
		bounds.Include(box.center + move - box.extent); bounds.Include(box.center + move + box.extent);
		std::optional<CollisionHit3> hit;if(!m_nodes.empty()) Visit(0, bounds, box, move, categories, ignore, hit);
		std::size_t offset=m_first.size();
		for(const auto& layer:m_layers) {
			auto candidate=layer->Cast(box,move,categories,ignore);
			if(candidate) {
				if(offset>std::numeric_limits<std::uint32_t>::max()-candidate->triangle) throw std::overflow_error("collision layer triangle index overflow");
				candidate->triangle+=std::uint32_t(offset);
				if(!hit || candidate->fraction<hit->fraction || candidate->fraction==hit->fraction &&
					(candidate->starts_overlapping>hit->starts_overlapping || candidate->starts_overlapping==hit->starts_overlapping && candidate->triangle<hit->triangle)) hit=candidate;
			}
			offset+=layer->TriangleCount();
		}return hit;
	}
private:
	struct Node {collision_detail::Bounds bounds; std::uint32_t begin{}, count{}, left{}, right{};};
	std::uint32_t Build(std::uint32_t begin, std::uint32_t count)
	{
		const auto index = static_cast<std::uint32_t>(m_nodes.size());
		const auto first = m_order[begin]; collision_detail::Bounds bounds{m_first[first], m_first[first]};
		for (std::uint32_t row = begin; row < begin + count; ++row) {
			const auto triangle = m_order[row]; bounds.Include(m_first[triangle]); bounds.Include(m_second[triangle]); bounds.Include(m_third[triangle]);
		}
		m_nodes.push_back({bounds, begin, count}); if (count <= 8) return index;
		const auto size = bounds.maximum - bounds.minimum;
		const unsigned axis = size.y > size.x ? (size.z > size.y ? 2 : 1) : (size.z > size.x ? 2 : 0);
		const auto component = [axis](FixedVector3 p) {return axis == 0 ? p.x : axis == 1 ? p.y : p.z;};
		std::sort(m_order.begin() + begin, m_order.begin() + begin + count, [&](auto a, auto b) {
			const auto ca = component(m_first[a]) + component(m_second[a]) + component(m_third[a]);
			const auto cb = component(m_first[b]) + component(m_second[b]) + component(m_third[b]);
			return ca != cb ? ca < cb : a < b;
		});
		const auto left = Build(begin, count / 2), right = Build(begin + count / 2, count - count / 2);
		m_nodes[index].left = left; m_nodes[index].right = right; m_nodes[index].count = 0; return index;
	}
	void Visit(std::uint32_t node, const collision_detail::Bounds &bounds, CollisionBox3 box, FixedVector3 move,
		std::uint32_t categories, std::optional<std::uint64_t> ignore, std::optional<CollisionHit3> &hit) const
	{
		const auto &entry = m_nodes[node]; if (!entry.bounds.Overlaps(bounds)) return;
		if (!entry.count) {Visit(entry.left, bounds, box, move, categories, ignore, hit); Visit(entry.right, bounds, box, move, categories, ignore, hit); return;}
		for (std::uint32_t row = entry.begin; row < entry.begin + entry.count; ++row) {
			const auto triangle = m_order[row]; if (!(m_categories[triangle] & categories) || (ignore && m_subjects[triangle] == *ignore)) continue;
			auto candidate = collision_detail::Sweep(m_first[triangle], m_second[triangle], m_third[triangle], box, move);
			if (!candidate) continue;
			candidate->triangle = triangle; candidate->subject = m_subjects[triangle]; candidate->surface = m_surfaces[triangle];
			if (!hit || candidate->fraction < hit->fraction ||
				(candidate->fraction == hit->fraction && (candidate->starts_overlapping > hit->starts_overlapping ||
				(candidate->starts_overlapping == hit->starts_overlapping && triangle < hit->triangle)))) hit = candidate;
		}
	}
	std::vector<FixedVector3> m_first, m_second, m_third;
	std::vector<std::uint64_t> m_subjects;
	std::vector<std::uint32_t> m_categories, m_surfaces, m_order;
	std::vector<Node> m_nodes;
	std::vector<std::shared_ptr<const CollisionScene3>> m_layers;
	bool m_ready{};
};
}
