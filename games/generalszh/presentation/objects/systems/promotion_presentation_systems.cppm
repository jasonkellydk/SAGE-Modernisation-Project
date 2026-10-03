export module games.generalszh.presentation.objects.systems.promotion_presentation_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.spatial.components.transform;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.rts.veterancy.resources.promotions;
export import engine.gameplay.common.healing.resources.heal_pulses;
export import engine.gameplay.common.identity.components.owner;
export import games.generalszh.presentation.objects.resources.world_animations;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
import Engine.Core.Math.FixedPresentation;

// Promotions seen and heard, once a tick after the simulation published them
// (Object::createVeterancyLevelFX): GameData's LevelGainAnimation where the
// unit is, rising LevelGainAnimationZRise a second for LevelGainAnimationTime
// and fading out at its end, and MiscAudio's UnitPromoted on it; none for an
// object IGNORED_IN_GUI, nor while icon UI is off (getDrawIconUI: the shell map turns it off). Its own promotion
// sound (ActiveBody::onVeterancyLevelChanged: SoundPromotedVeteran / Elite / Hero by the level reached) plays on it
// whatever the icon UI. World animations past their time go first. A single-burst heal (AutoHealBehavior::update with
// SingleBurst, while icon UI is on) shows GameData's GetHealedAnimation over each one it reached hurt, at its position
// raised by its geometry's height (getMaxHeightAbovePosition), rising GetHealedAnimationZRise a second for
// GetHealedAnimationTime and fading out (WORLD_ANIM_FADE_ON_EXPIRE).
export namespace generalszh::presentation
{
struct PromotionPresentationSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Transform>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Owner>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::Promotions>, ecs::Read<engine::gameplay::BurstHeals>, ecs::Read<LookCatalog>,
		ecs::Read<PresentationFrame>, ecs::Write<WorldAnimations>, ecs::Write<SoundRequests>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		const auto lookup = context.Lookup<Lookup>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const PresentationFrame &frame = context.Read<PresentationFrame>();
		const double clock = frame.clock;
		auto &animations = context.Write<WorldAnimations>().shown;
		auto &sounds = context.Write<SoundRequests>().pending;
		std::erase_if(animations, [&](const WorldAnimation &animation) { return clock >= animation.expire; });
		for (const engine::gameplay::Promotion &promotion : context.Read<engine::gameplay::Promotions>().list)
		{
			if (!lookup.IsAlive(promotion.entity))
				continue;
			const auto *transform = lookup.Get<engine::gameplay::Transform>(promotion.entity);
			const auto *definition = lookup.Get<engine::gameplay::DefinitionRef>(promotion.entity);
			const DefinitionLooks *looks = definition != nullptr ? catalog.Of(definition->index) : nullptr;
			const auto *owner = lookup.Get<engine::gameplay::Owner>(promotion.entity);
			if (transform == nullptr)
				continue;
			const std::array<float, 3> at{Engine::Math::ToFloat(transform->position.x), Engine::Math::ToFloat(transform->position.y),
				Engine::Math::ToFloat(transform->position.z)};
			if (looks != nullptr && promotion.to > promotion.from && promotion.to >= 1 && promotion.to <= 3 && !looks->promotedSounds[promotion.to - 1u].empty())
			{
				const auto *owner = lookup.Get<engine::gameplay::Owner>(promotion.entity);
				sounds.push_back({looks->promotedSounds[promotion.to - 1u], at, owner != nullptr ? owner->player : SoundRequest::NoOwner});
			}
			if (frame.drawIconUi && (looks == nullptr || !looks->ignoredInGui))
				ShowLevelGain(catalog, at, clock, animations, sounds, owner != nullptr ? owner->player : SoundRequest::NoOwner);
		}
		if (!frame.drawIconUi || catalog.getHealedAnimation.empty())
			return;
		context.Read<engine::gameplay::BurstHeals>().ForEach([&](ecs::Entity healed) {
			const auto *transform = lookup.IsAlive(healed) ? lookup.Get<engine::gameplay::Transform>(healed) : nullptr;
			if (transform == nullptr)
				return;
			const auto *definition = lookup.Get<engine::gameplay::DefinitionRef>(healed);
			const DefinitionLooks *looks = definition != nullptr ? catalog.Of(definition->index) : nullptr;
			const std::array<float, 3> at{Engine::Math::ToFloat(transform->position.x), Engine::Math::ToFloat(transform->position.y),
				Engine::Math::ToFloat(transform->position.z) + (looks != nullptr ? looks->constructionHeight : 0.0f)};
			animations.push_back({catalog.getHealedAnimation, at, clock, clock + catalog.getHealedSeconds, catalog.getHealedRise, true});
		});
	}

	// createVeterancyLevelFX's animation and sound at `at`.
	static void ShowLevelGain(const LookCatalog &catalog, const std::array<float, 3> &at, double clock, std::vector<WorldAnimation> &animations,
		std::vector<SoundRequest> &sounds, std::uint32_t owner = SoundRequest::NoOwner)
	{
		if (!catalog.levelGainAnimation.empty() && catalog.levelGainSeconds > 0.0f)
			animations.push_back({catalog.levelGainAnimation, at, clock, clock + catalog.levelGainSeconds, catalog.levelGainRise, true});
		if (!catalog.unitPromotedSound.empty())
			sounds.push_back({catalog.unitPromotedSound, at, owner});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::PromotionPresentationSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.promotions";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
