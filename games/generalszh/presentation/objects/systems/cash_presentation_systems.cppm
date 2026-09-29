export module games.generalszh.presentation.objects.systems.cash_presentation_systems;
import std;

export import engine.ecs.system.system;
export import engine.gameplay.common.spatial.components.transform;
export import games.generalszh.gameplay.powers.resources.cash_notices;
export import games.generalszh.presentation.objects.resources.floating_texts;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import engine.gameplay.rts.economy.components.auto_deposit;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.common.appearance.components.appearance;
export import engine.gameplay.common.identity.resources.relationships;
import Engine.Core.Math.FixedPresentation;

// Cash that changed hands, floating up once a tick after the simulation published it (InGameUI::addFloatingText, in
// the colour the original gives each): a kill's bounty (GUI:AddCash, yellow) over the killer
// (Player::doBountyForKill), a cash hack's take (GUI:AddCash, green) over the hacker and its loss (GUI:LoseCash, red)
// over the hacked (CashHackSpecialPower::doSpecialPowerAtObject).
export namespace generalszh::presentation
{
struct CashPresentationSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::Transform>>;
	using Resources = ecs::Resources<ecs::Read<generalszh::gameplay::CashNotices>, ecs::Read<FloatingTextSettings>, ecs::Write<FloatingTexts>>;

	void Execute(ecs::SystemContext &context) const
	{
		using Kind = generalszh::gameplay::CashNotice::Kind;
		const FloatingTextSettings &settings = context.Read<FloatingTextSettings>();
		FloatingTexts &texts = context.Write<FloatingTexts>();
		for (const generalszh::gameplay::CashNotice &notice : context.Read<generalszh::gameplay::CashNotices>().list)
		{
			const std::array<float, 3> at{Engine::Math::ToFloat(notice.position.x), Engine::Math::ToFloat(notice.position.y),
				Engine::Math::ToFloat(notice.position.z)};
			const std::array<float, 3> color = notice.kind == Kind::Bounty ? std::array<float, 3>{1.0f, 1.0f, 0.0f}
				: notice.kind == Kind::Stolen || notice.kind == Kind::Hacked ? std::array<float, 3>{0.0f, 1.0f, 0.0f}
																		   : std::array<float, 3>{1.0f, 0.0f, 0.0f};
			texts.shown.insert(texts.shown.begin(),
				FloatingText{FormatAmount(notice.kind == Kind::Lost ? settings.loseCash : settings.addCash, notice.amount), at, color, 230, 0});
		}
	}
};
}

export namespace generalszh::presentation
{
// AutoDepositUpdate::update / awardInitialCaptureBonus: a payment floats "+$amount" (GUI:AddCash) in its player's colour
// 10 above it, over a structure scattered up to 0.3 of its radii each way (GameClientRandomValue: whole units), only
// where the watcher may see it (Object::isLogicallyVisible: not stealthed and undetected from anyone but an ally); a
// capture bonus straight over it.
struct AutoDepositPresentationSystem
{
	using Query = ecs::Query<ecs::Read<engine::gameplay::DefinitionRef>>;
	using Lookup = ecs::Lookup<ecs::Read<engine::gameplay::DefinitionRef>, ecs::Read<engine::gameplay::Appearance>>;
	using Resources = ecs::Resources<ecs::Read<engine::gameplay::AutoDeposits>, ecs::Read<FloatingTextSettings>, ecs::Read<LookCatalog>,
		ecs::Read<PresentationFrame>, ecs::Read<engine::gameplay::Relationships>, ecs::Write<FloatingTexts>, ecs::Write<PresentationRandom>>;

	void Execute(Query &, ecs::SystemContext &context) const
	{
		const auto lookup = context.Lookup<Lookup>();
		const FloatingTextSettings &settings = context.Read<FloatingTextSettings>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const std::uint32_t viewer = context.Read<PresentationFrame>().viewer;
		const auto &relationships = context.Read<engine::gameplay::Relationships>();
		FloatingTexts &texts = context.Write<FloatingTexts>();
		auto &random = context.Write<PresentationRandom>().engine;
		for (const engine::gameplay::AutoDepositPayment &payment : context.Read<engine::gameplay::AutoDeposits>().list)
		{
			const auto *definition = lookup.IsAlive(payment.entity) ? lookup.Get<engine::gameplay::DefinitionRef>(payment.entity) : nullptr;
			const DefinitionLooks *looks = definition != nullptr ? catalog.Of(definition->index) : nullptr;
			std::array<float, 3> at{Engine::Math::ToFloat(payment.position.x), Engine::Math::ToFloat(payment.position.y), Engine::Math::ToFloat(payment.position.z)};
			if (payment.capture == 0)
			{
				if (const auto *look = lookup.IsAlive(payment.entity) ? lookup.Get<engine::gameplay::Appearance>(payment.entity) : nullptr;
					look != nullptr && look->Test(catalog.bits.stealthed) && !look->Test(catalog.bits.detected) && viewer != PresentationFrame::NoViewer &&
					!relationships.Allies(viewer, payment.player) && viewer != payment.player)
					continue;
				if (looks != nullptr && looks->structure)
				{
					const auto scatter = [&](float reach) {
						const int whole = static_cast<int>(reach);
						return static_cast<float>(whole <= -whole ? whole : std::uniform_int_distribution<int>(-whole, whole)(random));
					};
					at[0] += scatter(looks->majorRadius * 0.3f);
					at[1] += scatter(looks->minorRadius * 0.3f);
				}
			}
			const auto color = catalog.ColorOf(payment.player);
			texts.shown.insert(texts.shown.begin(), FloatingText{FormatAmount(settings.addCash, payment.amount), at, {color[0], color[1], color[2]}, 230, 0});
		}
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::AutoDepositPresentationSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.auto_deposits";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::CashPresentationSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.cash";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::Simulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
}
