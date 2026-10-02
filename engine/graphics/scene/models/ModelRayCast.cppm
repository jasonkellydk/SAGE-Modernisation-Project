export module Graphics.Scene.Models.RayCast;
import std;

export import Engine.Core.Math.Vector3;
export import Engine.Core.Math.AffineTransform3;
import Engine.Core.Math.Triangle3;

// Ray picking against model geometry, the original's W3D ray cast (RTS3DScene::castRay -> W3DHierarchyRenderObject::
// Cast_Ray -> W3DMeshRenderObject::Cast_Ray -> W3DMeshGeometry::Cast_Ray -> Triangle3::Intersect_Segment):
// - a model's collision shape is immutable per-model data, structure of arrays: its meshes' vertices (each mesh's own
//   space: a rigid mesh's bone, a skin's model space in its bind pose) and triangles;
// - a caster keeps the ray as a segment and the fraction of the nearest hit so far; every hit shortens it, so the
//   nearest triangle over every model tried wins whatever the order (as castRay refines its search);
// - a mesh is skipped when it is alpha blended (CheckTranslucent: Is_Alpha), hidden, or hidden by its animation; an
//   ALIGNED mesh is turned to face along the ray, an ORIENTED one to face the ray's start, before it is tested;
// - a model is tried only when the ray's line passes within its bounding sphere (castRay's ray-sphere test).
export namespace Graphics
{
namespace model_collision_flag
{
inline constexpr std::uint8_t Alpha = 1u << 0;    // blended without alpha test on pass 0 (Is_Alpha)
inline constexpr std::uint8_t Hidden = 1u << 1;   // W3D_MESH_FLAG_HIDDEN (Set_Hidden at load)
inline constexpr std::uint8_t Aligned = 1u << 2;  // W3DMeshResource::ALIGNED: faces the camera
inline constexpr std::uint8_t Oriented = 1u << 3; // W3DMeshResource::ORIENTED: turns toward the camera
}

struct ModelCollisionMesh
{
	std::uint32_t firstVertex{0};
	std::uint32_t vertexCount{0};
	std::uint32_t firstIndex{0}; // triangles: three indices each, relative to firstVertex
	std::uint32_t indexCount{0};
	std::uint8_t flags{0};
	std::uint8_t reserved[3]{};
};

// One model's collision geometry (its top level of detail's meshes, in the order the model draws them).
struct ModelCollisionShape
{
	std::vector<float> x;
	std::vector<float> y;
	std::vector<float> z;
	std::vector<std::uint32_t> indices;
	std::vector<ModelCollisionMesh> meshes;
};

// A ray being cast (castRay's tempRayTest): the segment, its end pulled in to the nearest hit after each model hit.
struct ModelRayCaster
{
	Engine::Math::Vector3 start;
	Engine::Math::Vector3 end;
};

// castRay's quick ray-sphere test (Graphics Gems I, p388): the ray's line (from its start, along its unit direction)
// passes within the sphere.
inline bool RayNearSphere(const ModelRayCaster &ray, Engine::Math::Vector3 center, float radius) noexcept
{
	const Engine::Math::Vector3 direction = (ray.end - ray.start).Normalized();
	const Engine::Math::Vector3 toCenter = center - ray.start;
	const float alpha = toCenter.Dot(direction);
	const float beta = radius * radius - (toCenter.Dot(toCenter) - alpha * alpha);
	return beta >= 0.0f;
}

// W3DMeshRenderObject::Cast_Ray for one mesh placed at `world`, visible (not hidden by its model or animation), on the
// segment of one model's cast: whether it holds a triangle nearer than `fraction` (the model's nearest so far on that
// segment, 1: none), which it then lowers (W3DMeshQueries' Collide: only a strictly nearer hit counts).
inline bool CastMeshRay(const ModelCollisionShape &shape, std::size_t meshIndex, Engine::Math::AffineTransform3 world, bool visible,
	const ModelRayCaster &ray, float &fraction)
{
	const ModelCollisionMesh &mesh = shape.meshes[meshIndex];
	if ((mesh.flags & model_collision_flag::Alpha) != 0 || (mesh.flags & model_collision_flag::Hidden) != 0 || !visible)
		return false;
	if ((mesh.flags & model_collision_flag::Aligned) != 0)
		world = Engine::Math::AffineTransform3::From_Forward_Direction(world.Translation(), (ray.end - ray.start).Normalized() * -1.0f);
	else if ((mesh.flags & model_collision_flag::Oriented) != 0)
		world = Engine::Math::AffineTransform3::From_Forward_Direction(world.Translation(), ray.start - world.Translation());
	const auto toObject = world.Inverse();
	if (!toObject)
		return false;
	// The segment in the mesh's space (fractions carry over: the map is affine).
	const Engine::Math::Vector3 start = toObject->Transform_Point(ray.start);
	const Engine::Math::Vector3 stop = toObject->Transform_Point(ray.end);
	const auto vertex = [&](std::uint32_t index) {
		const std::size_t at = static_cast<std::size_t>(mesh.firstVertex) + index;
		return Engine::Math::Vector3{shape.x[at], shape.y[at], shape.z[at]};
	};
	bool hit = false;
	for (std::uint32_t index = mesh.firstIndex; index + 3 <= mesh.firstIndex + mesh.indexCount; index += 3)
	{
		const auto found = Engine::Math::Triangle3::Intersect_Segment(start, stop, vertex(shape.indices[index]), vertex(shape.indices[index + 1]),
			vertex(shape.indices[index + 2]));
		if (found && found->fraction < fraction)
		{
			fraction = found->fraction;
			hit = true;
		}
	}
	return hit;
}

// W3DHierarchyRenderObject::Cast_Ray, then castRay's refinement: one model's meshes (each at its world transform in
// `worlds`, shown per `visible`) against the caster's segment; a hit pulls the segment's end in to it. Whether it hit.
inline bool CastModelRay(const ModelCollisionShape &shape, std::span<const Engine::Math::AffineTransform3> worlds, std::span<const std::uint8_t> visible,
	ModelRayCaster &ray)
{
	float fraction = 1.0f;
	bool hit = false;
	for (std::size_t mesh = 0; mesh < shape.meshes.size() && mesh < worlds.size(); ++mesh)
		hit |= CastMeshRay(shape, mesh, worlds[mesh], mesh < visible.size() ? visible[mesh] != 0 : true, ray, fraction);
	if (hit)
		ray.end = ray.start + (ray.end - ray.start) * fraction;
	return hit;
}
}
