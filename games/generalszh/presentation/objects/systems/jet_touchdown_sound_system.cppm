export module games.generalszh.presentation.objects.systems.jet_touchdown_sound_system;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.aircraft.resources.jet_touchdowns;
export import engine.gameplay.common.identity.components.definition_ref;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
import Engine.Core.Math.FixedPresentation;

// JetTakeoffOrLandingState::update (JetAIUpdate.cpp): a jet touching down on its landing plays MiscAudio's
// AircraftWheelScreech at its position (setPosition: heard by everyone in earshot), once a landing; once a tick, on the
// simulation's touchdowns.
export namespace generalszh::presentation
{
struct JetTouchdownSoundSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::JetTouchdowns>, ecs::Read<LookCatalog>, ecs::Write<SoundRequests>>;

	void Execute(ecs::SystemContext &context) const
	{
		const std::string &screech = context.Read<LookCatalog>().aircraftWheelScreechSound;
		if (screech.empty())
			return;
		auto &sounds = context.Write<SoundRequests>().pending;
		for (const engine::gameplay::JetTouchdown &touchdown : context.Read<engine::gameplay::JetTouchdowns>().list)
			sounds.push_back({screech, {Engine::Math::ToFloat(touchdown.position.x), Engine::Math::ToFloat(touchdown.position.y),
				Engine::Math::ToFloat(touchdown.position.z)}});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::JetTouchdownSoundSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.jet_touchdown_sounds";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
