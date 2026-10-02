export module engine.gameplay.rts.combat.components.deploy;
import std;

export import engine.ecs.core.component_registry;

// A unit that must set up before it fires and pack up before it moves (the original's DeployStyleAIUpdate): its
// state, the tick its unpacking or packing is done (0: none under way), and its rules (UnpackTime, PackTime,
// TurretsFunctionOnlyWhenDeployed, TurretsMustCenterBeforePacking, ManualDeployAnimations).
export namespace engine::gameplay
{
enum class DeployState : std::uint8_t
{
	ReadyToMove,     // packed
	Deploy,          // unpacking
	ReadyToAttack,   // deployed
	Undeploy,        // packing
	AligningTurrets, // deployed, its turret swinging back to rest before packing
};

struct Deploy
{
	std::uint64_t unpackTicks{0};
	std::uint64_t packTicks{0};
	std::uint64_t waitTick{0}; // m_frameToWaitForDeploy
	DeployState state{DeployState::ReadyToMove};
	bool turretsOnlyWhenDeployed{false};
	bool turretsMustCenter{false};
	bool manualAnimations{false};
	std::uint8_t reserved[4]{}; // no padding: checkpoints hold its bytes
};
}

export namespace ecs
{
template<>
struct ComponentTraits<engine::gameplay::Deploy>
{
	static constexpr std::string_view StableName = "engine.gameplay.deploy";
	static constexpr std::uint32_t Version = 1;
	static constexpr PersistencePolicy Persistence = PersistencePolicy::Serializable;
};
}
