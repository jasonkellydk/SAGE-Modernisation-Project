export module games.generalszh.presentation.objects.systems.detector_ping_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.rts.stealth.resources.detections;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
import Engine.Core.Math.FixedPresentation;

// Stealth detectors' scans, shown and heard once a tick after the simulation
// published them (one pass: the particle world and the sound requests are
// its side effects): each scan an IR ping (bright when it found something)
// and a beacon at the detector's bone, a ping sound (loud when it found
// something), and an IR grid under each thing it revealed (snapped to a
// 12-unit grid, 17 above the detector, as the original).
export namespace generalszh::presentation
{
struct DetectorPingSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::DetectorPings>, ecs::Read<engine::gameplay::Detections>, ecs::Read<LookCatalog>,
		ecs::Read<BonePoses>, ecs::Write<ParticleWorldHandle>, ecs::Write<SoundRequests>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		ParticleWorldHandle &particles = context.Write<ParticleWorldHandle>();
		if (particles.world == nullptr || particles.content == nullptr)
			return;
		const auto lookup = context.Lookup<Lookup>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const BonePoses &poses = context.Read<BonePoses>();
		auto &sounds = context.Write<SoundRequests>().pending;
		const auto lookOf = [&](ecs::Entity entity) -> const DefinitionLooks::DetectorLook * {
			const auto *definition = lookup.Get<engine::gameplay::DefinitionRef>(entity);
			const DefinitionLooks *looks = definition != nullptr ? catalog.Of(definition->index) : nullptr;
			return looks != nullptr && looks->detector ? &*looks->detector : nullptr;
		};
		const auto start = [&](const std::string &system, std::array<float, 3> at, float facing) {
			if (const auto *definition = system.empty() ? nullptr : particles.content->particles.Find(system))
				particles.world->Create(*definition, engine::effects::EmitterTransform::At(at[0], at[1], at[2], facing));
		};
		context.Read<engine::gameplay::DetectorPings>().ForEach([&](const engine::gameplay::DetectorPing &ping) {
			const DefinitionLooks::DetectorLook *look = lookOf(ping.detector);
			const auto *transform = lookup.Get<engine::gameplay::Transform>(ping.detector);
			if (look == nullptr || transform == nullptr)
				return;
			const float facing = static_cast<float>(transform->facing.units) * 6.283185307179586f / 4294967296.0f;
			// At its bone (the original's default spot when it has none).
			std::array<float, 3> local{-1.66f, 5.5f, 15.0f};
			const auto *definition = lookup.Get<engine::gameplay::DefinitionRef>(ping.detector);
			if (const DefinitionLooks *looks = catalog.Of(definition->index); looks != nullptr && !look->bone.empty() && poses.pose &&
				!looks->stateLooks.empty() && looks->stateLooks[0] < catalog.lookModels.size())
				if (const BoneLookup bone = poses.pose(catalog.lookModels[looks->stateLooks[0]], look->bone); bone.found)
					local = bone.position;
			const float c = std::cos(facing), s = std::sin(facing);
			const std::array<float, 3> at{Engine::Math::ToFloat(transform->position.x) + c * local[0] - s * local[1],
				Engine::Math::ToFloat(transform->position.y) + s * local[0] + c * local[1], Engine::Math::ToFloat(transform->position.z) + local[2]};
			start(ping.found != 0 ? look->brightPing : look->ping, at, facing);
			start(look->beacon, at, facing);
			const std::string &sound = ping.found != 0 ? look->loudPingSound : look->pingSound;
			if (!sound.empty())
				sounds.push_back({sound, at});
		});
		for (const engine::gameplay::Detection &detection : context.Read<engine::gameplay::Detections>().All())
		{
			const DefinitionLooks::DetectorLook *look = lookOf(detection.detector);
			const auto *target = lookup.Get<engine::gameplay::Transform>(detection.target);
			const auto *detector = lookup.Get<engine::gameplay::Transform>(detection.detector);
			if (look == nullptr || look->grid.empty() || target == nullptr || detector == nullptr)
				continue;
			const float x = Engine::Math::ToFloat(target->position.x), y = Engine::Math::ToFloat(target->position.y);
			start(look->grid, {x - std::fmod(std::trunc(x), 12.0f), y - std::fmod(std::trunc(y), 12.0f), Engine::Math::ToFloat(detector->position.z) + 17.0f},
				0.0f);
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::DetectorPingSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.detector_pings";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
