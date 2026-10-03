export module games.renegade.content.levels.persist_records;
import std;
export import Assets.Adapters.W3D.Chunks;
export import Engine.Core.Math.FixedAffineTransform3;
export import Engine.Core.Math.FixedOrientedBox3;

export namespace renegade::content::persist
{
using Bytes = Assets::W3D::W3DByteSpan;
using Chunk = Assets::W3D::W3DChunkView;
using Micro = Assets::W3D::W3DMicroChunkView;

inline std::expected<std::vector<Chunk>, std::string> Children(Bytes bytes)
{
	std::vector<Chunk> result;
	if (!Assets::W3D::W3DVisit_Chunks(bytes, [&](const Chunk &chunk) { result.push_back(chunk); return true; }))
		return std::unexpected("truncated persist chunk envelope");
	return result;
}
inline std::expected<Chunk, std::string> One(Bytes bytes, std::uint32_t id)
{
	const auto children = Children(bytes);
	if (!children) return std::unexpected(children.error());
	std::optional<Chunk> result;
	for (const auto &chunk : *children) if (chunk.id == id) {
		if (result) return std::unexpected("duplicate persist chunk " + std::to_string(id));
		result = chunk;
	}
	if (!result) return std::unexpected("missing persist chunk " + std::to_string(id));
	return *result;
}
inline std::expected<std::vector<Micro>, std::string> Micros(Bytes bytes)
{
	std::vector<Micro> result;
	if (!Assets::W3D::W3DVisit_MicroChunks(bytes, [&](const Micro &chunk) { result.push_back(chunk); return true; }))
		return std::unexpected("truncated persist microchunk envelope");
	return result;
}
inline std::expected<std::uint32_t, std::string> U32(Bytes bytes)
{
	std::uint32_t result{};
	if (bytes.size() != 4 || !Assets::W3D::W3DRead_U32(bytes, 0, result)) return std::unexpected("expected legacy uint32");
	return result;
}
inline std::expected<Engine::Math::Fixed, std::string> Scalar(Bytes bytes)
{
	const auto bits = U32(bytes);
	if (!bits || ((*bits >> 23) & 255) == 255) return std::unexpected("expected finite legacy scalar");
	const auto value = Engine::Math::Fixed::FromBinary32Bits(*bits);
	if (value == Engine::Math::Fixed::Min() || value == Engine::Math::Fixed::Max()) return std::unexpected("legacy scalar exceeds fixed-point range");
	return value;
}
inline std::expected<bool, std::string> Flag(Bytes bytes)
{
	if (bytes.size() != 1 || std::to_integer<unsigned>(bytes.front()) > 1) return std::unexpected("expected legacy boolean");
	return bytes.front() != std::byte{};
}
inline std::expected<std::string, std::string> Text(Bytes bytes)
{
	if (bytes.empty() || bytes.back() != std::byte{} ||
		std::find(bytes.begin(), bytes.end() - 1, std::byte{}) != bytes.end() - 1)
		return std::unexpected("expected terminated legacy string");
	return std::string(reinterpret_cast<const char *>(bytes.data()), bytes.size() - 1);
}
inline std::expected<Engine::Math::FixedAffineTransform3, std::string> Matrix(Bytes bytes)
{
	if (bytes.size() != 48) return std::unexpected("expected legacy Matrix3D (12 floats)");
	Engine::Math::FixedAffineTransform3 result;
	for (std::size_t i = 0; i < result.elements.size(); ++i) {
		const auto value = Scalar(bytes.subspan(i * 4, 4));
		if (!value) return std::unexpected(value.error());
		result.elements[i] = *value;
	}
	return result;
}
inline std::expected<Engine::Math::FixedVector3, std::string> Vector(Bytes bytes)
{
	if (bytes.size() != 12) return std::unexpected("expected legacy Vector3 (3 floats)");
	const auto x = Scalar(bytes.first(4)), y = Scalar(bytes.subspan(4, 4)), z = Scalar(bytes.last(4));
	if (!x || !y || !z) return std::unexpected("invalid legacy Vector3 scalar");
	return Engine::Math::FixedVector3{*x, *y, *z};
}
inline std::expected<Engine::Math::FixedOrientedBox3, std::string> OrientedBox(Bytes bytes)
{
	// wwmath/obbox.h: Matrix3 basis (row major), Vector3 center, Vector3 extent.
	if (bytes.size() != 60) return std::unexpected("expected legacy OBBox (15 floats)");
	Engine::Math::FixedOrientedBox3 box;
	for (std::size_t column = 0; column < 3; ++column) {
		const auto x = Scalar(bytes.subspan(column * 4, 4)), y = Scalar(bytes.subspan((3 + column) * 4, 4)), z = Scalar(bytes.subspan((6 + column) * 4, 4));
		if (!x || !y || !z) return std::unexpected("invalid legacy OBBox basis");
		box.axes[column] = {*x, *y, *z};
	}
	const auto center = Vector(bytes.subspan(36, 12)), extent = Vector(bytes.subspan(48, 12));
	if (!center || !extent) return std::unexpected("invalid legacy OBBox center or extent");
	box.center = *center; box.extent = *extent;
	if (!box.IsValid()) return std::unexpected("legacy OBBox requires an orthonormal basis and nonnegative extents");
	return box;
}
// Search only a known homogeneous schema subtree. Conversation categories
// contain a raw integer prefix and must never be passed to this function.
inline std::expected<std::vector<Chunk>, std::string> Descendants(Bytes bytes, std::uint32_t id, unsigned depth = 0)
{
	if (depth > 48) return std::unexpected("persist nesting budget exceeded");
	const auto children = Children(bytes);
	if (!children) return std::unexpected(children.error());
	std::vector<Chunk> result;
	for (const auto &chunk : *children) {
		if (chunk.id == id) result.push_back(chunk);
		else if (chunk.contains_children) {
			auto nested = Descendants(chunk.payload, id, depth + 1);
			if (!nested) return std::unexpected(nested.error());
			result.insert(result.end(), nested->begin(), nested->end());
		}
	}
	return result;
}
// SimplePersistFactoryClass writes fixed 32-bit opaque tokens, independent of
// the host pointer width. No original factory or pointer is ever executed.
inline std::expected<Chunk, std::string> ObjectData(Bytes factory)
{
	const auto pointer = One(factory, 0x00100100);
	if (!pointer || !U32(pointer->payload)) return std::unexpected("invalid legacy object remap token");
	return One(factory, 0x00100101);
}
// Inherited classes can reuse an ID in their own chunk namespace. Locate a
// variable record beside the base class's parent tag, not by a global ID scan.
inline std::expected<std::vector<Chunk>, std::string> ScopedVariables(Bytes bytes, std::uint32_t parent_id,
	std::uint32_t variables_id, unsigned depth = 0)
{
	if (depth > 48) return std::unexpected("persist nesting budget exceeded");
	const auto children = Children(bytes); if (!children) return std::unexpected(children.error());
	std::vector<Chunk> result;
	if (std::ranges::any_of(*children, [&](const Chunk &chunk) { return chunk.id == parent_id; })) {
		for (const auto &chunk : *children) if (chunk.id == variables_id) result.push_back(chunk);
	} else for (const auto &chunk : *children) if (chunk.contains_children) {
		auto nested = ScopedVariables(chunk.payload, parent_id, variables_id, depth + 1);
		if (!nested) return std::unexpected(nested.error());
		result.insert(result.end(), nested->begin(), nested->end());
	}
	return result;
}
}
