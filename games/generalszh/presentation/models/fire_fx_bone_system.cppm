export module games.generalszh.presentation.models.fire_fx_bone_system;
import std;

export import games.generalszh.presentation.models.model_library;
export import games.generalszh.presentation.objects.systems.effect_attachment_systems;
export import games.generalszh.presentation.objects.systems.object_presentation_systems;
import engine.ecs.system.system;
import engine.gameplay.common.identity.components.definition_ref;

// Where each weapon's fire FX plays, from the firer's model as drawn this frame (W3DModelDraw::handleWeaponFireFX:
// FXList::doFXPos at m_renderObject->Get_Bone_Transform(info.m_fxBone), the firing barrel's FireFX bone with the model's
// animation, turret, pitch and recoil as it is drawn). The barrel's bone is NAME01, NAME02... (NAME itself when
// unnumbered; a barrel past the numbered ones takes the first). A request this places is done; one whose model has not
// loaded yet, or has no such bone, is left to FireFxPlacementSystem (the bone's rest place turned by hand, else the
// firer's own position, as the original falls back to the logic position).
export namespace generalszh::presentation
{
struct FireFxBoneSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using SideTables = ecs::SideTables<ecs::Read<WeaponPose>>;
	using Resources = ecs::Resources<ecs::Read<PresentedObjects>, ecs::Read<ObjectInstances>, ecs::Read<LookCatalog>, ecs::Write<ModelLibrary>,
		ecs::Write<FxRequests>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		FxRequests &requests = context.Write<FxRequests>();
		if (std::none_of(requests.pending.begin(), requests.pending.end(), [](const FxRequest &request) { return request.firedBy.IsValid(); }))
			return;
		const LookCatalog &catalog = context.Read<LookCatalog>();
		ModelLibrary &library = context.Write<ModelLibrary>();
		const auto &weapons = context.SideRead<SideTables, WeaponPose>();
		std::vector<ObjectInstance> drawn;
		context.Read<ObjectInstances>().AppendTo(drawn);
		context.Read<PresentedObjects>().ForEach([&](const PresentedObject &object) {
			const DefinitionLooks *looks = catalog.Of(object.definition);
			if (looks == nullptr || looks->states.Empty() || object.state >= looks->states.states.size())
				return;
			const auto instance = std::find_if(drawn.begin(), drawn.end(), [&](const ObjectInstance &candidate) { return candidate.key == object.key; });
			if (instance == drawn.end() || instance->look >= library.entryOfLook.size())
				return;
			const std::uint32_t entry = library.entryOfLook[instance->look];
			if (entry >= library.entries.size() || library.loads[entry].status != ModelStatus::Ready)
				return;
			ModelEntry &model = library.entries[entry];
			const WeaponPose *weapon = weapons.Get(object.entity);
			const content::ModelState &state = looks->states.states[object.state];
			for (FxRequest &request : requests.pending)
			{
				if (request.firedBy != object.entity)
					continue;
				const std::string &slotBone = state.slotFireFxBones[request.firedSlot < 3 ? request.firedSlot : 0];
				const std::string &name = slotBone.empty() ? state.fireFxBone : slotBone;
				if (name.empty())
					continue;
				const std::uint32_t barrel = weapon != nullptr ? weapon->barrel : 0u;
				char numbered[128];
				std::snprintf(numbered, sizeof numbered, "%s%02u", name.c_str(), barrel + 1u);
				auto place = InstanceBoneOf(model, *instance, numbered);
				if (!place)
					place = InstanceBoneOf(model, *instance, name);
				if (!place)
				{
					std::snprintf(numbered, sizeof numbered, "%s01", name.c_str());
					place = InstanceBoneOf(model, *instance, numbered);
				}
				if (!place)
					continue;
				request.at = place->at;
				request.yaw = place->yaw;
				request.firedBy = {};
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::FireFxBoneSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.fire_fx_bones";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<generalszh::presentation::FireFxPlacementSystem>;
	using After = SystemTypeList<generalszh::presentation::ObjectPresentationSystem, generalszh::presentation::DamageEffectSystem, generalszh::presentation::MountedDrawSystem>;
};
}
