export module games.generalszh.presentation.objects.resources.object_frame;
import std;

export import engine.ecs.system.chunk_outputs;
export import engine.ecs.core.entity;
export import games.generalszh.presentation.objects.resources.object_instance;
export import engine.gameplay.common.appearance.components.appearance;
import engine.ecs.system.system;

// What presentation produces for the frame (per chunk, in chunk order): each
// visible object as the renderer draws it, and the same objects as the
// effects and sound read them (where, in which look, in what condition).
export namespace generalszh::presentation
{

struct PresentedObject
{
	ecs::Entity entity;
	std::uint64_t key{0}; // the simulation entity (index and generation)
	std::uint32_t definition{0};
	std::uint32_t look{0};
	std::uint32_t state{0}; // the model state its conditions pick
	std::array<float, 3> position{};
	float facing{0.0f};
	float scale{1.0f};
	engine::gameplay::Appearance appearance;
	bool moving{false};
	bool turning{false}; // its turret turned in the last tick
	bool drawn{true};    // its model is drawn this frame (not hidden by stealth, not a fallen tree gone)
};

// The drawing of a portable structure mounted on a carrier (W3DDependencyModelDraw), drawn only if its carrier is
// (`carrier`: its key, as PresentedObject's).
struct MountedInstance
{
	std::uint64_t carrier{0};
	ObjectInstance instance;
};

struct ObjectInstances : ecs::ChunkOutputs<ObjectInstance>
{
};

struct PresentedObjects : ecs::ChunkOutputs<PresentedObject>
{
};

struct MountedInstances : ecs::ChunkOutputs<MountedInstance>
{
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::presentation::ObjectInstances>
{
	static constexpr std::string_view StableName = "generalszh.presentation.object_instances";
};
template<>
struct ResourceTraits<generalszh::presentation::PresentedObjects>
{
	static constexpr std::string_view StableName = "generalszh.presentation.presented_objects";
};
template<>
struct ResourceTraits<generalszh::presentation::MountedInstances>
{
	static constexpr std::string_view StableName = "generalszh.presentation.mounted_instances";
};
}
