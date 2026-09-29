export module games.generalszh.presentation.objects.systems.crate_presentation_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.spatial.components.transform;
export import games.generalszh.gameplay.crates.resources.crates;
export import games.generalszh.presentation.objects.resources.floating_texts;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.systems.promotion_presentation_systems;
import Engine.Core.Math.FixedPresentation;

// Crates picked up, seen and heard, once a tick after the simulation
// published its pickups (after the floating texts aged):
//   salvage fitted (SalvageCrateCollide armor / weapons): MiscAudio's
//   CrateSalvage on the picker;
//   money (a salvage crate's, a money crate's): MiscAudio's CrateMoney on the
//   picker, and a salvage crate floats "+$amount" in the picker's colour 10
//   above the crate (SalvageCrateCollide::doMoney; a money crate floats none);
//   a level: the promotion's animation and sound (doLevelGain -> createVeterancyLevelFX);
//   the crate's ExecuteAnimation where it was, for its time, rising and fading as it says;
//   the crate's ExecuteFX on the picker (CrateCollide::onCollide doFXObj).
export namespace generalszh::presentation
{
struct CratePresentationSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Transform>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Transform>>;
	using Resources = ecs::Resources<ecs::Read<generalszh::gameplay::CratePickups>, ecs::Read<LookCatalog>, ecs::Read<FloatingTextSettings>,
		ecs::Write<FloatingTexts>, ecs::Write<SoundRequests>, ecs::Write<FxRequests>, ecs::Read<PresentationFrame>,
		ecs::Write<WorldAnimations>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		using Kind = generalszh::gameplay::CratePickup::Kind;
		const auto lookup = context.Lookup<Lookup>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const FloatingTextSettings &settings = context.Read<FloatingTextSettings>();
		FloatingTexts &texts = context.Write<FloatingTexts>();
		auto &sounds = context.Write<SoundRequests>().pending;
		auto &fx = context.Write<FxRequests>().pending;
		auto &animations = context.Write<WorldAnimations>().shown;
		const double clock = context.Read<PresentationFrame>().clock;
		const bool icons = context.Read<PresentationFrame>().drawIconUi;
		for (const generalszh::gameplay::CratePickup &pickup : context.Read<generalszh::gameplay::CratePickups>().list)
		{
			const auto *transform = lookup.IsAlive(pickup.picker) ? lookup.Get<engine::gameplay::Transform>(pickup.picker) : nullptr;
			const std::array<float, 3> crate{Engine::Math::ToFloat(pickup.position.x), Engine::Math::ToFloat(pickup.position.y),
				Engine::Math::ToFloat(pickup.position.z)};
			const std::array<float, 3> picker = transform != nullptr
				? std::array<float, 3>{Engine::Math::ToFloat(transform->position.x), Engine::Math::ToFloat(transform->position.y), Engine::Math::ToFloat(transform->position.z)}
				: crate;
			if (pickup.kind == Kind::Salvage && !catalog.crateSalvageSound.empty())
				sounds.push_back({catalog.crateSalvageSound, picker});
			else if (pickup.kind == Kind::Unit && !catalog.crateFreeUnitSound.empty())
				sounds.push_back({catalog.crateFreeUnitSound, picker});
			else if (pickup.kind == Kind::Money)
			{
				if (!catalog.crateMoneySound.empty())
					sounds.push_back({catalog.crateMoneySound, picker});
				if (pickup.floats && pickup.amount > 0)
				{
					const auto color = catalog.ColorOf(pickup.player);
					texts.shown.insert(texts.shown.begin(),
						FloatingText{FormatAmount(settings.addCash, pickup.amount), {crate[0], crate[1], crate[2] + 10.0f}, {color[0], color[1], color[2]}, 230, 0});
				}
			}
			// With icon UI on only (CrateCollide::onCollide, createVeterancyLevelFX).
			if (pickup.kind == Kind::Level && icons)
				PromotionPresentationSystem::ShowLevelGain(catalog, picker, clock, animations, sounds);
			if (icons && !pickup.animation.empty() && pickup.animationSeconds > Engine::Math::Fixed{})
				animations.push_back({pickup.animation, crate, clock, clock + Engine::Math::ToFloat(pickup.animationSeconds),
					Engine::Math::ToFloat(pickup.animationRise), pickup.animationFades});
			if (!pickup.fx.empty())
			{
				FxRequest request{pickup.fx, picker, transform != nullptr ? static_cast<float>(transform->facing.units) * 6.283185307179586f / 4294967296.0f : 0.0f};
				request.object = pickup.picker;
				fx.push_back(std::move(request));
			}
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::CratePresentationSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.crates";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
