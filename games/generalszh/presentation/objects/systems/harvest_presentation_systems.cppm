export module games.generalszh.presentation.objects.systems.harvest_presentation_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.rts.harvesting.resources.harvest_catalog;
export import engine.gameplay.common.identity.components.definition_ref;
export import games.generalszh.presentation.objects.resources.floating_texts;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
import Engine.Core.Math.FixedPresentation;

// The supply economy, seen and heard, once a tick after the simulation
// published its harvest events: first every floating text ages
// (InGameUI::updateFloatingText); then a delivery floats "+$amount" in its
// player's colour over the truck (SupplyCenterDockUpdate::action's floating
// text), and a truck that emptied a warehouse with no other near says so
// (SupplyTruckAIUpdate's SuppliesDepletedVoice).
export namespace generalszh::presentation
{
struct HarvestPresentationSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::HarvestEvents>, ecs::Read<LookCatalog>, ecs::Read<FloatingTextSettings>,
		ecs::Write<FloatingTexts>, ecs::Write<SoundRequests>>;

	static void Age(FloatingTexts &texts, const FloatingTextSettings &settings)
	{
		for (auto text = texts.shown.begin(); text != texts.shown.end();)
		{
			++text->ticks;
			if (text->ticks > settings.timeoutTicks)
			{
				// REAL_TO_INT((frame - timeout) * vanishRate), taken off the alpha every frame.
				text->alpha -= static_cast<int>(static_cast<float>(text->ticks - settings.timeoutTicks) * settings.vanishPerTick);
				if (text->alpha <= 0)
				{
					text = texts.shown.erase(text);
					continue;
				}
			}
			++text;
		}
	}

	void Execute(Query &, ecs::SystemContext &context) const
	{
		const auto lookup = context.Lookup<Lookup>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const FloatingTextSettings &settings = context.Read<FloatingTextSettings>();
		FloatingTexts &texts = context.Write<FloatingTexts>();
		auto &sounds = context.Write<SoundRequests>().pending;
		Age(texts, settings);
		context.Read<engine::gameplay::HarvestEvents>().ForEach([&](const engine::gameplay::HarvestEvent &event) {
			const std::array<float, 3> at{Engine::Math::ToFloat(event.position.x), Engine::Math::ToFloat(event.position.y), Engine::Math::ToFloat(event.position.z)};
			if (event.kind == engine::gameplay::HarvestEvent::Kind::Delivered)
			{
				const auto color = catalog.ColorOf(event.player);
				texts.shown.insert(texts.shown.begin(), FloatingText{FormatAmount(settings.addCash, event.amount), at, {color[0], color[1], color[2]}, 230, 0});
				return;
			}
			const auto *definition = lookup.IsAlive(event.harvester) ? lookup.Get<engine::gameplay::DefinitionRef>(event.harvester) : nullptr;
			const DefinitionLooks *looks = definition != nullptr ? catalog.Of(definition->index) : nullptr;
			if (looks != nullptr && !looks->suppliesDepletedVoice.empty())
				sounds.push_back({looks->suppliesDepletedVoice, at});
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::HarvestPresentationSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.harvest";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
