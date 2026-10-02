export module games.generalszh.presentation.rendering.bridge_models;
import std;

export import games.generalszh.presentation.objects.resources.bridge_art;
export import games.generalszh.content.terrain.bridge_content;
export import engine.filesystem.core.virtual_file_system;
import games.generalszh.content.install.asset_paths;
import Assets.Adapters.W3D.Chunks;
import Assets.Adapters.W3D.Mesh;
import Engine.Core.Math.FixedPresentation;

// The map-drawn bridges' art read from the install (W3DBridge::load for each Roads.ini Bridge and damage state): the
// state's model (BridgeModelName, ...Damaged, ...ReallyDamaged, ...Broken) from Art/W3D, its sub-objects whose names
// start with `<model>.BRIDGE_LEFT`, `.BRIDGE_SPAN` and `.BRIDGE_RIGHT` (case blind; a later one wins, as the original's
// walk of the sub-objects) placed by the bones they hang from at rest, each made from the mesh of that very name (or,
// when no sub-object names one, the mesh `<model>.BRIDGE_LEFT` itself, unplaced); its texture the state's Roads.ini
// Texture. A state whose model is not there, or has no BRIDGE_LEFT mesh, does not load.
export namespace generalszh::presentation
{
namespace bridge_models_detail
{
using Bytes = std::span<const std::byte>;
using Matrix = std::array<float, 12>; // row-major 3x4

inline std::string Upper(std::string_view text)
{
	std::string upper(text);
	std::transform(upper.begin(), upper.end(), upper.begin(), [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
	return upper;
}

inline std::uint32_t U32(Bytes bytes, std::size_t at)
{
	std::uint32_t value = 0;
	Assets::W3D::W3DRead_U32(bytes, at, value);
	return value;
}

inline float F32(Bytes bytes, std::size_t at)
{
	float value = 0.0f;
	Assets::W3D::W3DRead_F32(bytes, at, value);
	return value;
}

inline std::string Name(Bytes bytes, std::size_t at, std::size_t length)
{
	return Upper(Assets::W3D::W3DRead_Fixed_String(bytes, at, length));
}

// Each chunk of `bytes`, one level down.
template<typename Visit>
void Chunks(Bytes bytes, Visit &&visit)
{
	Assets::W3D::W3DVisit_Chunks(bytes, [&](const Assets::W3D::W3DChunkView &chunk) {
		visit(chunk.id, chunk.payload);
		return true;
	});
}

// Matrix3D(Quaternion, translation): a pivot's own transform.
inline Matrix FromPivot(const std::array<float, 4> &q, const std::array<float, 3> &t)
{
	const float x = q[0], y = q[1], z = q[2], w = q[3];
	return {1.0f - 2.0f * (y * y + z * z), 2.0f * (x * y - z * w), 2.0f * (z * x + y * w), t[0],
		2.0f * (x * y + z * w), 1.0f - 2.0f * (z * z + x * x), 2.0f * (y * z - x * w), t[1],
		2.0f * (z * x - y * w), 2.0f * (y * z + x * w), 1.0f - 2.0f * (y * y + x * x), t[2]};
}

inline Matrix Multiply(const Matrix &a, const Matrix &b)
{
	Matrix out{};
	for (std::size_t row = 0; row < 3; ++row)
	{
		for (std::size_t column = 0; column < 3; ++column)
			out[row * 4 + column] = a[row * 4] * b[column] + a[row * 4 + 1] * b[4 + column] + a[row * 4 + 2] * b[8 + column];
		out[row * 4 + 3] = a[row * 4] * b[3] + a[row * 4 + 1] * b[7] + a[row * 4 + 2] * b[11] + a[row * 4 + 3];
	}
	return out;
}

inline std::optional<std::vector<std::byte>> ReadModel(const engine::filesystem::VirtualFileSystem &files, std::string_view name)
{
	for (const std::string &path : content::AssetCandidates(content::AssetFolder::Models, std::string(name) + ".w3d"))
		if (auto bytes = files.Read(path))
			return bytes;
	return std::nullopt;
}

// Every pivot at rest in the model's frame (its own hierarchy, or the one its HLOD names).
inline std::vector<Matrix> RestPivots(const engine::filesystem::VirtualFileSystem &files, std::string_view model, Bytes bytes)
{
	std::optional<Bytes> hierarchy;
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
		if ((external = ReadModel(files, hierarchyName)))
			Chunks(*external, [&](std::uint32_t id, Bytes payload) {
				if (id == 0x100)
					hierarchy = payload;
			});
	std::vector<Matrix> pivots;
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
		Matrix pose = FromPivot({F32(table, at + 44), F32(table, at + 48), F32(table, at + 52), F32(table, at + 56)},
			{F32(table, at + 20), F32(table, at + 24), F32(table, at + 28)});
		if (parent != 0xFFFFFFFFu && parent < pivots.size())
			pose = Multiply(pivots[parent], pose);
		pivots.push_back(pose);
	}
	return pivots;
}

// The mesh of that full name (`container.mesh`, case blind) in the model's file.
inline std::optional<BridgeMesh> FindMesh(Bytes bytes, std::string_view fullName)
{
	std::optional<BridgeMesh> found;
	Chunks(bytes, [&](std::uint32_t id, Bytes payload) {
		if (id != 0x0 || found)
			return;
		Assets::W3D::W3DParsedMesh mesh;
		std::string error;
		if (!Assets::W3D::W3DParse_Mesh(payload, mesh, error))
			return;
		const std::string own = Upper(mesh.header.name), container = Upper(mesh.header.container_name);
		if ((container.empty() ? own : container + "." + own) != fullName)
			return;
		BridgeMesh made;
		made.positions.reserve(mesh.positions.size());
		for (const auto &p : mesh.positions)
			made.positions.push_back({p.x, p.y, p.z});
		for (const auto &n : mesh.normals)
			made.normals.push_back({n.x, n.y, n.z});
		for (const auto &uv : mesh.stage_texcoords)
			made.uvs.push_back({uv.x, uv.y});
		for (const auto &triangle : mesh.triangles)
			made.triangles.push_back(triangle);
		found = std::move(made);
	});
	return found;
}
}

// W3DBridge::load(state) for one model name (empty or not there: not loaded).
inline BridgeModel LoadBridgeModel(const engine::filesystem::VirtualFileSystem &files, std::string_view model, std::string_view texture)
{
	using namespace bridge_models_detail;
	BridgeModel loaded;
	loaded.texture = std::string(texture);
	if (model.empty())
		return loaded;
	const auto bytes = ReadModel(files, model);
	if (!bytes)
		return loaded;
	const std::string upper = Upper(model);
	const std::array<std::string, 3> wanted{upper + ".BRIDGE_LEFT", upper + ".BRIDGE_SPAN", upper + ".BRIDGE_RIGHT"};
	// The HLOD's sub-objects: which bone each hangs from.
	std::array<std::string, 3> names = wanted;
	std::array<std::optional<std::uint32_t>, 3> bones{};
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
				for (std::size_t piece = 0; piece < wanted.size(); ++piece)
					if (name.starts_with(wanted[piece]))
					{
						names[piece] = name;
						bones[piece] = U32(sub, 0);
					}
			});
		});
	});
	const auto pivots = RestPivots(files, model, *bytes);
	std::array<std::optional<BridgeMesh>, 3> meshes;
	for (std::size_t piece = 0; piece < wanted.size(); ++piece)
	{
		meshes[piece] = FindMesh(*bytes, names[piece]);
		if (meshes[piece] && bones[piece] && *bones[piece] < pivots.size())
			meshes[piece]->rest = pivots[*bones[piece]];
	}
	if (!meshes[0])
		return loaded;
	loaded.loaded = true;
	loaded.left = std::move(meshes[0]);
	loaded.span = std::move(meshes[1]);
	loaded.right = std::move(meshes[2]);
	return loaded;
}

// Every Roads.ini Bridge's art (the catalog's order: the simulation's bridge template indices).
inline BridgeArt LoadBridgeArt(const engine::filesystem::VirtualFileSystem &files, const content::BridgeCatalog &catalog)
{
	BridgeArt art;
	art.kinds.reserve(catalog.size());
	for (const auto &[name, bridge] : catalog)
	{
		BridgeKind kind;
		kind.scale = Engine::Math::ToFloat(bridge.scale);
		for (std::size_t state = 0; state < BridgeDamageStates; ++state)
			kind.states[state] = LoadBridgeModel(files, bridge.models[state], bridge.textures[state]);
		art.kinds.push_back(std::move(kind));
	}
	return art;
}
}
