export module games.generalszh.content.models.model_rigs;
import std;

export import Engine.Core.Math.FixedVector;
export import Engine.Core.Math.FixedAngle;
import engine.filesystem.core.virtual_file_system;
import games.generalszh.content.install.asset_paths;

// Where a model's bones are at rest, for the simulation (the original's
// logic reads them too: airfield parking places and runways, ...): read
// straight from the W3D hierarchy (its own, or the one its HLOD names) and
// put together in fixed point, so every peer gets the same positions.
// Floats in the file are decoded bit by bit, never as floats.
export namespace generalszh::content
{
struct RestBone
{
	Engine::Math::FixedVector3 position;
	Engine::Math::TurnAngle facing; // the bone's turn around z
};

namespace model_rigs_detail
{
using Bytes = std::span<const std::byte>;
using Engine::Math::Fixed;

std::uint32_t U32(Bytes bytes, std::size_t at)
{
	if (at + 4 > bytes.size())
		return 0;
	return std::to_integer<std::uint32_t>(bytes[at]) | std::to_integer<std::uint32_t>(bytes[at + 1]) << 8 |
		std::to_integer<std::uint32_t>(bytes[at + 2]) << 16 | std::to_integer<std::uint32_t>(bytes[at + 3]) << 24;
}

// An IEEE-754 single as fixed point, exactly (truncated below the fixed step).
Fixed DecodeSingle(std::uint32_t bits)
{
	const bool negative = (bits >> 31) != 0;
	const std::int32_t exponent = static_cast<std::int32_t>((bits >> 23) & 0xFF);
	std::uint64_t mantissa = bits & 0x7FFFFF;
	if (exponent == 0 && mantissa == 0)
		return {};
	if (exponent == 0xFF)
		return {}; // not a number or infinite: nothing sensible
	if (exponent != 0)
		mantissa |= 0x800000;
	// value = mantissa * 2^(exponent - 150); raw = value * 2^FractionBits
	const std::int32_t shift = (exponent == 0 ? -149 : exponent - 150) + Fixed::FractionBits;
	std::int64_t raw = 0;
	if (shift >= 0)
		raw = shift >= 38 ? std::numeric_limits<std::int64_t>::max() : static_cast<std::int64_t>(mantissa << shift);
	else if (shift > -64)
		raw = static_cast<std::int64_t>(mantissa >> -shift);
	return Fixed::FromRaw(negative ? -raw : raw);
}

Fixed F32(Bytes bytes, std::size_t at) { return DecodeSingle(U32(bytes, at)); }

std::string Name(Bytes bytes, std::size_t at, std::size_t length)
{
	std::string name;
	for (std::size_t index = 0; index < length && at + index < bytes.size(); ++index)
	{
		const char c = static_cast<char>(std::to_integer<unsigned char>(bytes[at + index]));
		if (c == '\0')
			break;
		name.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
	}
	return name;
}

// Calls visit(id, payload) for each chunk in `bytes` (one level).
template<typename Visit>
void Chunks(Bytes bytes, Visit &&visit)
{
	std::size_t at = 0;
	while (at + 8 <= bytes.size())
	{
		const std::uint32_t id = U32(bytes, at);
		const std::uint32_t size = U32(bytes, at + 4) & 0x7FFFFFFFu;
		if (at + 8 + size > bytes.size())
			return;
		visit(id, bytes.subspan(at + 8, size));
		at += 8 + size;
	}
}

struct Quaternion
{
	Fixed x, y, z, w{Fixed::One()};
};

Quaternion Multiply(const Quaternion &a, const Quaternion &b)
{
	return {a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
		a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z};
}

Engine::Math::FixedVector3 Rotate(const Quaternion &q, const Engine::Math::FixedVector3 &v)
{
	// v + 2w (q x v) + 2 q x (q x v)
	const Engine::Math::FixedVector3 u{q.x, q.y, q.z};
	const auto cross = [](const Engine::Math::FixedVector3 &a, const Engine::Math::FixedVector3 &b) {
		return Engine::Math::FixedVector3{a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
	};
	const auto t = cross(u, v);
	const Engine::Math::FixedVector3 t2{t.x + t.x, t.y + t.y, t.z + t.z};
	const auto c = cross(u, t2);
	return {v.x + q.w * t2.x + c.x, v.y + q.w * t2.y + c.y, v.z + q.w * t2.z + c.z};
}
}

class ModelRigs
{
public:
	explicit ModelRigs(const engine::filesystem::VirtualFileSystem &files) : m_files(&files) {}

	// How many barrels a weapon has on `model`, as the original's validateWeaponBarrelInfo:
	// NAME01, NAME02... while any of the names has that bone, else 1 when any has
	// the unadorned bone; 0 when none is there.
	std::uint32_t BarrelCount(std::string_view model, std::span<const std::string> names)
	{
		std::uint32_t count = 0;
		for (int index = 1; index <= 99; ++index)
		{
			bool found = false;
			for (const std::string &name : names)
			{
				char suffix[4];
				std::snprintf(suffix, sizeof(suffix), "%02d", index);
				found = found || (!name.empty() && Bone(model, name + suffix).has_value());
			}
			if (!found)
				break;
			count = static_cast<std::uint32_t>(index);
		}
		if (count == 0)
			for (const std::string &name : names)
				if (!name.empty() && Bone(model, name).has_value())
					return 1;
		return count;
	}

	// A model's bone at rest (names are case blind); none when the model or bone is not there.
	std::optional<RestBone> Bone(std::string_view model, std::string_view bone)
	{
		const auto &bones = Load(model);
		const auto found = bones.find(Upper(bone));
		return found != bones.end() ? std::optional(found->second) : std::nullopt;
	}

	// The least and greatest y of a model's sub-object mesh named `<model>.<prefix>...` (the name's start, case blind;
	// a later level of detail's match wins, as the original's walk of the sub-objects), its vertices placed by its bone
	// at rest (W3DBridge::load's m_minY / m_maxY of BRIDGE_LEFT). None when the model or mesh is not there.
	std::optional<std::pair<Engine::Math::Fixed, Engine::Math::Fixed>> MeshExtentY(std::string_view model, std::string_view prefix)
	{
		using namespace model_rigs_detail;
		auto bytes = ReadModel(model);
		if (!bytes)
			return std::nullopt;
		const std::string wanted = Upper(model) + "." + Upper(prefix);
		// The HLOD's sub-objects: which bone each hangs from.
		std::string subName;
		std::uint32_t bone = 0;
		Chunks(*bytes, [&](std::uint32_t id, Bytes payload) {
			if (id != 0x700)
				return;
			Chunks(payload, [&](std::uint32_t lod, Bytes array) {
				if (lod != 0x702)
					return;
				Chunks(array, [&](std::uint32_t inner, Bytes sub) {
					if (inner != 0x704 || sub.size() < 36)
						return;
					const std::string name = Name(sub, 4, 32);
					if (name.starts_with(wanted))
					{
						subName = name;
						bone = U32(sub, 0);
					}
				});
			});
		});
		// The mesh itself: its header's container and mesh names make its full name.
		std::optional<Bytes> vertices;
		Chunks(*bytes, [&](std::uint32_t id, Bytes mesh) {
			if (id != 0x0 || vertices)
				return;
			std::string full;
			std::optional<Bytes> points;
			Chunks(mesh, [&](std::uint32_t inner, Bytes payload) {
				if (inner == 0x1F && payload.size() >= 40)
				{
					const std::string container = Name(payload, 24, 16), own = Name(payload, 8, 16);
					full = container.empty() ? own : container + "." + own;
				}
				else if (inner == 0x2)
					points = payload;
			});
			if (points && !full.empty() && (subName.empty() ? full.starts_with(wanted) : full == subName))
				vertices = points;
		});
		if (!vertices || vertices->size() < 12)
			return std::nullopt;
		const auto poses = Poses(model, *bytes);
		const Pose at = !subName.empty() && bone < poses.size() ? poses[bone] : Pose{};
		std::optional<std::pair<Engine::Math::Fixed, Engine::Math::Fixed>> extent;
		for (std::size_t offset = 0; offset + 12 <= vertices->size(); offset += 12)
		{
			const Engine::Math::FixedVector3 local{F32(*vertices, offset), F32(*vertices, offset + 4), F32(*vertices, offset + 8)};
			const Engine::Math::Fixed y = Rotate(at.rotation, local).y + at.position.y;
			if (!extent)
				extent = std::pair{y, y};
			extent->first = std::min(extent->first, y);
			extent->second = std::max(extent->second, y);
		}
		return extent;
	}

private:
	using BoneTable = std::map<std::string, RestBone, std::less<>>;
	using Bytes = std::span<const std::byte>;

	struct Pose
	{
		Engine::Math::FixedVector3 position;
		model_rigs_detail::Quaternion rotation;
	};

	// The model's hierarchy (its own, or the one its HLOD names).
	std::optional<std::vector<std::byte>> HierarchyOf(std::string_view model, Bytes bytes, std::optional<Bytes> &hierarchy)
	{
		using namespace model_rigs_detail;
		std::string hierarchyName;
		Chunks(bytes, [&](std::uint32_t id, Bytes payload) {
			if (id == 0x100)
				hierarchy = payload;
			else if (id == 0x700)
				Chunks(payload, [&](std::uint32_t inner, Bytes header) {
					if (inner == 0x701)
						hierarchyName = Name(header, 24, 16);
				});
		});
		std::optional<std::vector<std::byte>> external;
		if (!hierarchy && !hierarchyName.empty() && hierarchyName != Upper(model))
			if ((external = ReadModel(hierarchyName)))
				Chunks(*external, [&](std::uint32_t id, Bytes payload) {
					if (id == 0x100)
						hierarchy = payload;
				});
		return external;
	}

	// Every pivot's rest pose in the model's frame and its name, by pivot index.
	std::vector<std::pair<std::string, Pose>> Pivots(std::string_view model, Bytes bytes)
	{
		using namespace model_rigs_detail;
		std::optional<Bytes> hierarchy;
		const auto external = HierarchyOf(model, bytes, hierarchy); // keeps an external hierarchy's bytes alive
		std::vector<std::pair<std::string, Pose>> pivots;
		if (!hierarchy)
			return pivots;
		Bytes table;
		Chunks(*hierarchy, [&](std::uint32_t id, Bytes payload) {
			if (id == 0x102)
				table = payload;
		});
		for (std::size_t at = 0; at + 60 <= table.size(); at += 60)
		{
			const std::uint32_t parent = U32(table, at + 16);
			const Engine::Math::FixedVector3 local{F32(table, at + 20), F32(table, at + 24), F32(table, at + 28)};
			const Quaternion rotation{F32(table, at + 44), F32(table, at + 48), F32(table, at + 52), F32(table, at + 56)};
			Pose pose{local, rotation};
			if (parent != 0xFFFFFFFFu && parent < pivots.size())
			{
				const Pose &up = pivots[parent].second;
				const auto moved = Rotate(up.rotation, local);
				pose = {{up.position.x + moved.x, up.position.y + moved.y, up.position.z + moved.z}, Multiply(up.rotation, rotation)};
			}
			pivots.emplace_back(Name(table, at, 16), pose);
		}
		return pivots;
	}

	std::vector<Pose> Poses(std::string_view model, Bytes bytes)
	{
		std::vector<Pose> poses;
		for (const auto &[name, pose] : Pivots(model, bytes))
			poses.push_back(pose);
		return poses;
	}

	static std::string Upper(std::string_view text)
	{
		std::string upper(text);
		std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
		return upper;
	}

	std::optional<std::vector<std::byte>> ReadModel(std::string_view name) const
	{
		for (const std::string &path : AssetCandidates(AssetFolder::Models, std::string(name) + ".w3d"))
			if (auto bytes = m_files->Read(path))
				return bytes;
		return std::nullopt;
	}

	const BoneTable &Load(std::string_view model)
	{
		const std::string key = Upper(model);
		if (const auto found = m_models.find(key); found != m_models.end())
			return found->second;
		BoneTable &table = m_models[key];
		using namespace model_rigs_detail;
		auto bytes = ReadModel(model);
		if (!bytes)
			return table;
		for (const auto &[name, pose] : Pivots(model, *bytes))
		{
			const auto &q = pose.rotation;
			const Fixed two = Fixed::FromInt(2);
			const Engine::Math::TurnAngle facing = Engine::Math::Atan2(two * (q.w * q.z + q.x * q.y), Fixed::One() - two * (q.y * q.y + q.z * q.z));
			if (!name.empty())
				table.emplace(name, RestBone{pose.position, facing});
		}
		return table;
	}

	const engine::filesystem::VirtualFileSystem *m_files;
	std::map<std::string, BoneTable, std::less<>> m_models;
};
}
