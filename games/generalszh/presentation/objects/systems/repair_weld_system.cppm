export module games.generalszh.presentation.objects.systems.repair_weld_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.slaves.resources.slave_orders;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.spatial.components.transform;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.resources.object_frame;
export import games.generalszh.presentation.objects.algorithms.draw_bones;
import Engine.Core.Math.FixedPresentation;

// SlavedUpdate::setRepairState(REPAIRSTATE_WELDING), once a tick after the simulation: each weld a repairing drone started
// makes its RepairWeldingSys where its RepairWeldingFXBone sits in its pristine model (getPristineBonePositions: in the
// model's own frame) plus the drone's position (the original does not turn it by the drone's facing), or at the drone
// when it has no such bone; its particles live the weld's frames times LOGICFRAMES_PER_SECOND (setLifetimeRange, as the
// original), and MiscAudio's RepairSparks plays there. Nothing (no sound either) when the system is not known.
export namespace generalszh::presentation
{
// Where a weld's sparks start: the bone (model frame, unturned) on the drone, else the drone itself.
inline std::array<float, 3> WeldSparksAt(const std::array<float, 3> &drone, const BoneLookup &bone) noexcept
{
	if (!bone.found)
		return drone;
	return {drone[0] + bone.position[0], drone[1] + bone.position[1], drone[2] + bone.position[2]};
}

struct RepairWeldSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Transform>>;
	using SideTables = ecs::SideTables<ecs::Read<ExtraShownLooks>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::SlaveOrders>, ecs::Read<LookCatalog>, ecs::Read<PresentedObjects>, ecs::Read<BonePoses>,
		ecs::Read<PresentationFrame>, ecs::Write<ParticleWorldHandle>, ecs::Write<SoundRequests>>;

	void Execute(ecs::SystemContext &context) const
	{
		const auto &welds = context.Read<engine::gameplay::SlaveOrders>().welds;
		if (welds.empty())
			return;
		ParticleWorldHandle &particles = context.Write<ParticleWorldHandle>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const BonePoses &poses = context.Read<BonePoses>();
		const PresentationFrame &frame = context.Read<PresentationFrame>();
		const auto lookup = context.Lookup<Lookup>();
		const auto &extras = context.SideRead<SideTables, ExtraShownLooks>();
		auto &sounds = context.Write<SoundRequests>().pending;
		for (const engine::gameplay::RepairWeld &weld : welds)
		{
			if (!lookup.IsAlive(weld.slave))
				continue;
			const auto *ref = lookup.Get<engine::gameplay::DefinitionRef>(weld.slave);
			const auto *transform = lookup.Get<engine::gameplay::Transform>(weld.slave);
			const DefinitionLooks *looks = ref != nullptr ? catalog.Of(ref->index) : nullptr;
			if (transform == nullptr || looks == nullptr || looks->weldingSystem.empty())
				continue;
			const auto *definition = particles.world != nullptr && particles.content != nullptr ? particles.content->particles.Find(looks->weldingSystem) : nullptr;
			if (definition == nullptr)
				continue;
			const std::array<float, 3> drone{Engine::Math::ToFloat(transform->position.x), Engine::Math::ToFloat(transform->position.y),
				Engine::Math::ToFloat(transform->position.z)};
			BoneLookup bone;
			if (!looks->weldingBone.empty())
				context.Read<PresentedObjects>().ForEach([&](const PresentedObject &object) {
					if (object.entity == weld.slave && !bone.found)
						bone = FindDrawBone(DrawBoneSearch{poses, catalog, *looks, object, extras.Get(weld.slave), frame.clock}, looks->weldingBone, false);
				});
			const std::array<float, 3> at = WeldSparksAt(drone, bone);
			const auto id = particles.world->Create(*definition, engine::effects::EmitterTransform::At(at[0], at[1], at[2], 0.0f));
			particles.world->SetParticleLifetime(id, static_cast<float>(weld.lifetime));
			if (!catalog.repairSparksSound.empty())
				sounds.push_back({catalog.repairSparksSound, at});
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::RepairWeldSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.repair_welds";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
