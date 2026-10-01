export module games.generalszh.presentation.objects.systems.radius_decal_view_system;
import std;

export import engine.ecs.system.system;
export import games.generalszh.presentation.objects.resources.radius_cursor;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.interaction.resources.interaction_resources;
export import games.generalszh.gameplay.effects.components.radius_decal;
export import games.generalszh.gameplay.effects.resources.radius_decal_looks;
export import games.generalszh.gameplay.powers.components.spectre_gunship;
export import games.generalszh.gameplay.objects.resources.object_templates;
export import engine.gameplay.common.identity.components.owner;
export import engine.gameplay.common.identity.components.definition_ref;
export import engine.gameplay.rts.vision.components.dynamic_clearing;
export import engine.gameplay.common.spatial.components.transform;
import Engine.Core.Math.FixedPresentation;

// The objects' radius decals as seen this frame (RadiusDecal::update on the objects' RadiusDecals): each laid decal
// its look's texture and style at its spot, a square of twice its radius, coloured by its Color (none: the colour of
// the player it was made for), its opacity throbbing with the logic frame; one only its owner may see shows to no one
// else (createRadiusDecal made none). A Spectre gunship's two (SpectreGunshipUpdate: its AttackAreaDecal over its first
// target, AttackAreaRadius, and its TargetingReticleDecal over where it aims, TargetingReticleRadius) while it inserts
// and orbits; cleaned up as it departs (a gunship shot down keeps them where they were until it is gone). A clearing
// range swelling (DynamicShroudClearingRangeUpdate: a spy satellite's scan, a spy drone) shows its grid while it grows:
// GRID_FX_DECAL_COUNT (30) of its GridDecalTemplate, radius 100, around it on its ring (sin, cos of each 30th of a turn,
// the angle added up in floats), each snapped down to a multiple of 23 (C's remainder of the truncated coordinate), at
// its opacity x 255 truncated (setOpacity: its template's throb is not played).
export namespace generalszh::presentation
{
inline std::array<float, 3> ToFloats(const Engine::Math::FixedVector3 &at) noexcept
{
	return {Engine::Math::ToFloat(at.x), Engine::Math::ToFloat(at.y), Engine::Math::ToFloat(at.z)};
}

// One decal's view (none when only its owner may see it and that is not the local player).
inline void AddRadiusDecalView(std::vector<RadiusDecalView> &views, const content::RadiusDecalLook &look, const std::array<float, 3> &at,
	float radius, std::uint32_t player, const LocalPlayer &local, const LookCatalog &catalog, const PresentationFrame &frame)
{
	if (!look.Present() || !(radius > 0.0f) || (look.onlyOwner && (!local.valid || local.player != player)))
		return;
	RadiusDecalView view;
	view.texture = look.texture;
	view.additive = look.additive;
	view.at = at;
	view.radius = radius;
	if (look.hasColor)
		view.color = {look.color[0] / 255.0f, look.color[1] / 255.0f, look.color[2] / 255.0f};
	else
	{
		const auto color = catalog.ColorOf(player);
		view.color = {color[0], color[1], color[2]};
	}
	view.opacity = RadiusDecalOpacity(look, frame.tick, frame.drawIconUi);
	views.push_back(std::move(view));
}

struct RadiusDecalViewSystem
{
	using Query = ecs::Query<ecs::Read<gameplay::RadiusDecal>>;
	using Resources = ecs::Resources<ecs::Read<gameplay::RadiusDecalLooks>, ecs::Read<LocalPlayer>, ecs::Read<LookCatalog>, ecs::Read<PresentationFrame>,
		ecs::Write<RadiusDecalViews>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		auto &views = context.Write<RadiusDecalViews>().decals;
		views.clear();
		const auto &looks = context.Read<gameplay::RadiusDecalLooks>();
		const LocalPlayer &local = context.Read<LocalPlayer>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const PresentationFrame &frame = context.Read<PresentationFrame>();
		query.ForEachChunk([&](auto chunk) {
			for (const gameplay::RadiusDecal &decal : chunk.template Get<gameplay::RadiusDecal>())
			{
				if (const content::RadiusDecalLook *look = looks.At(decal.look))
					AddRadiusDecalView(views, *look, ToFloats(decal.at), Engine::Math::ToFloat(decal.radius), decal.player, local, catalog, frame);
			}
		});
	}
};

struct SpectreDecalViewSystem
{
	using Query = ecs::Query<ecs::Read<gameplay::SpectreGunship>, ecs::Read<engine::gameplay::Owner>, ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<gameplay::ObjectTemplates>, ecs::Read<LocalPlayer>, ecs::Read<LookCatalog>, ecs::Read<PresentationFrame>,
		ecs::Write<RadiusDecalViews>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		auto &views = context.Write<RadiusDecalViews>().decals;
		const auto &templates = context.Read<gameplay::ObjectTemplates>();
		const LocalPlayer &local = context.Read<LocalPlayer>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const PresentationFrame &frame = context.Read<PresentationFrame>();
		query.ForEachChunk([&](auto chunk) {
			const auto ships = chunk.template Get<gameplay::SpectreGunship>();
			const auto owners = chunk.template Get<engine::gameplay::Owner>();
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			for (std::size_t row = 0; row < ships.size(); ++row)
			{
				const gameplay::SpectreGunship &ship = ships[row];
				if (ship.status != gameplay::GunshipStatus::Inserting && ship.status != gameplay::GunshipStatus::Orbiting)
					continue;
				const gameplay::SpectreGunshipConfig *config = templates.SpectreGunshipOf(definitions[row].index);
				if (config == nullptr)
					continue;
				AddRadiusDecalView(views, config->attackAreaDecal, ToFloats(ship.initialTarget), Engine::Math::ToFloat(config->attackAreaRadius), owners[row].player,
					local, catalog, frame);
				AddRadiusDecalView(views, config->reticleDecal, ToFloats(ship.reticle), Engine::Math::ToFloat(config->reticleRadius), owners[row].player, local,
					catalog, frame);
			}
		});
	}
};

struct GridDecalViewSystem
{
	static constexpr int Pieces = 30; // GRID_FX_DECAL_COUNT
	using Query = ecs::Query<ecs::Read<engine::gameplay::DynamicClearing>, ecs::Read<engine::gameplay::Transform>, ecs::Read<engine::gameplay::Owner>,
		ecs::Read<engine::gameplay::DefinitionRef>>;
	using Resources = ecs::Resources<ecs::Read<LocalPlayer>, ecs::Read<LookCatalog>, ecs::Read<PresentationFrame>, ecs::Write<RadiusDecalViews>>;

	void Execute(Query &query, ecs::SystemContext &context) const
	{
		auto &views = context.Write<RadiusDecalViews>().decals;
		const LocalPlayer &local = context.Read<LocalPlayer>();
		const LookCatalog &catalog = context.Read<LookCatalog>();
		const PresentationFrame &frame = context.Read<PresentationFrame>();
		query.ForEachChunk([&](auto chunk) {
			const auto clearings = chunk.template Get<engine::gameplay::DynamicClearing>();
			const auto transforms = chunk.template Get<engine::gameplay::Transform>();
			const auto owners = chunk.template Get<engine::gameplay::Owner>();
			const auto definitions = chunk.template Get<engine::gameplay::DefinitionRef>();
			for (std::size_t row = 0; row < clearings.size(); ++row)
			{
				const engine::gameplay::DynamicClearing &clearing = clearings[row];
				const DefinitionLooks *looks = catalog.Of(definitions[row].index);
				if (clearing.grid != 1 || looks == nullptr)
					continue;
				const float centreX = Engine::Math::ToFloat(transforms[row].position.x), centreY = Engine::Math::ToFloat(transforms[row].position.y);
				const float radius = Engine::Math::ToFloat(clearing.gridRadius);
				const float step = (std::numbers::pi_v<float> * 2.0f) / static_cast<float>(Pieces);
				const std::size_t first = views.size();
				float angle = 0.0f;
				for (int piece = 0; piece < Pieces; ++piece)
				{
					float x = centreX + std::sin(angle) * radius;
					float y = centreY + std::cos(angle) * radius;
					x -= static_cast<float>(static_cast<std::int32_t>(x) % 23);
					y -= static_cast<float>(static_cast<std::int32_t>(y) % 23);
					AddRadiusDecalView(views, looks->gridDecal, {x, y, 0.0f}, 100.0f, owners[row].player, local, catalog, frame);
					angle += step;
				}
				const std::int32_t opacity = static_cast<std::int32_t>(255.0f * Engine::Math::ToFloat(clearing.gridOpacity));
				for (std::size_t view = first; view < views.size(); ++view)
					views[view].opacity = opacity;
			}
		});
	}
};
}

export namespace ecs
{
template<>
struct SystemTraits<generalszh::presentation::RadiusDecalViewSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.radius_decal_view";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<>;
};
template<>
struct SystemTraits<generalszh::presentation::SpectreDecalViewSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.spectre_decal_view";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<generalszh::presentation::RadiusDecalViewSystem>;
};
template<>
struct SystemTraits<generalszh::presentation::GridDecalViewSystem>
{
	static constexpr std::string_view StableName = "generalszh.presentation.grid_decal_view";
	static constexpr bool Batch = true;
	static constexpr SystemPhase Phase = SystemPhase::PostSimulation;
	using Before = SystemTypeList<>;
	using After = SystemTypeList<generalszh::presentation::SpectreDecalViewSystem>;
};
}
