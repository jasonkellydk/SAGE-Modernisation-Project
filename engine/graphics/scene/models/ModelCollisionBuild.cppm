export module Graphics.Scene.Models.CollisionBuild;
import std;

export import Graphics.Scene.Models.RayCast;
import Graphics.Scene.Models.PartBones;
import Assets.Models;

// A model's collision shape from its asset, as the original's W3D meshes are tested (W3DMeshRenderObject::Cast_Ray):
// one collision mesh per source mesh, made of that mesh's triangles (its pass 0 submeshes, in order; every pass draws
// the same triangles), in the mesh's own space (a rigid mesh's bone, a skin's bind pose), on the bone its HLOD
// attachment names (Model_Part_Bone, as the model draws); flagged as W3DMeshRenderObject::Load_W3D reads the mesh:
// Alpha when any of its polygons blends on pass 0 without alpha test, Hidden (W3D_MESH_FLAG_HIDDEN), Aligned / Oriented
// (its geometry type camera aligned / oriented).
export namespace Graphics
{
inline constexpr std::uint32_t W3D_Mesh_Hidden = 0x00001000u;
inline constexpr std::uint32_t W3D_Mesh_Geometry_Type_Mask = 0x00FF0000u;
inline constexpr std::uint32_t W3D_Mesh_Camera_Aligned = 0x00010000u;
inline constexpr std::uint32_t W3D_Mesh_Camera_Oriented = 0x00060000u;

// `bones`: each collision mesh's bone. False (and `error` says why) when a mesh has no attachment, as the model's
// drawing refuses it.
inline bool Build_Model_Collision_Shape(const Assets::ModelAsset &model, ModelCollisionShape &shape, std::vector<std::uint32_t> &bones, std::string &error)
{
	shape = {};
	bones.clear();
	error.clear();
	const auto vertices = model.Vertices();
	const auto indices = model.Indices();
	const auto submeshes = model.Submeshes();
	for (std::size_t first = 0; first < submeshes.size();)
	{
		// The source mesh: consecutive submeshes of one name.
		std::size_t end = first + 1;
		while (end < submeshes.size() && submeshes[end].name == submeshes[first].name)
			++end;
		const auto bone = Model_Part_Bone(model.Rig(), submeshes[first].name);
		if (!bone)
		{
			error = "model part has no hierarchy attachment";
			return false;
		}
		ModelCollisionMesh mesh;
		mesh.firstVertex = static_cast<std::uint32_t>(shape.x.size());
		mesh.firstIndex = static_cast<std::uint32_t>(shape.indices.size());
		const std::uint32_t attributes = submeshes[first].source_attributes;
		if ((attributes & W3D_Mesh_Hidden) != 0)
			mesh.flags |= model_collision_flag::Hidden;
		if ((attributes & W3D_Mesh_Geometry_Type_Mask) == W3D_Mesh_Camera_Aligned)
			mesh.flags |= model_collision_flag::Aligned;
		else if ((attributes & W3D_Mesh_Geometry_Type_Mask) == W3D_Mesh_Camera_Oriented)
			mesh.flags |= model_collision_flag::Oriented;
		for (std::size_t index = first; index < end; ++index)
		{
			const Assets::ModelSubmesh &submesh = submeshes[index];
			if (submesh.pass != 0)
				continue;
			if (submesh.blends)
				mesh.flags |= model_collision_flag::Alpha;
			if (submesh.first_index > indices.size() || submesh.index_count > indices.size() - submesh.first_index)
			{
				error = "invalid model submesh";
				return false;
			}
			for (const std::uint32_t source : indices.subspan(submesh.first_index, submesh.index_count))
			{
				if (source >= vertices.size())
				{
					error = "model references an invalid vertex";
					return false;
				}
				shape.indices.push_back(static_cast<std::uint32_t>(shape.x.size()) - mesh.firstVertex);
				shape.x.push_back(vertices[source].position.x);
				shape.y.push_back(vertices[source].position.y);
				shape.z.push_back(vertices[source].position.z);
			}
		}
		mesh.vertexCount = static_cast<std::uint32_t>(shape.x.size()) - mesh.firstVertex;
		mesh.indexCount = static_cast<std::uint32_t>(shape.indices.size()) - mesh.firstIndex;
		shape.meshes.push_back(mesh);
		bones.push_back(bone->first);
		first = end;
	}
	return true;
}
}
