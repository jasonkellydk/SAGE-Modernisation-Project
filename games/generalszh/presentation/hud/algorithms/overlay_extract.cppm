export module games.generalszh.presentation.hud.algorithms.overlay_extract;
import std;

export import engine.ecs.core.world;
export import games.generalszh.session.session_view;
export import games.generalszh.content.global.in_game_ui;
export import games.generalszh.content.images.animation_2d;
export import games.generalszh.presentation.hud.resources.in_game_overlay;
import Engine.Core.Math.FixedPresentation;
import engine.gameplay.common.spatial.components.transform;
import engine.gameplay.common.spatial.systems.snapshot_system;
import engine.gameplay.common.identity.components.definition_ref;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.health.components.health;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.common.weapons.components.armament;
import engine.gameplay.common.weapons.components.weapon_slots;
import engine.gameplay.common.weapons.resources.weapon_catalog;
import engine.gameplay.rts.veterancy.components.experience;
import engine.gameplay.rts.death.components.dying;
import games.generalszh.content.objects.object_definition;
import games.generalszh.hud.superweapon_timers;
import games.generalszh.presentation.objects.resources.presentation_resources;
import games.generalszh.presentation.objects.resources.look_catalog;
import games.generalszh.presentation.objects.resources.floating_texts;
import games.generalszh.presentation.objects.resources.world_animations;
import games.generalszh.presentation.interaction.resources.interaction_resources;
import games.generalszh.presentation.interaction.components.selected;
import games.generalszh.presentation.hud.resources.in_game_messages;
import games.generalszh.presentation.hud.resources.named_timers;
import games.generalszh.presentation.hud.resources.screen_fade;
import games.generalszh.presentation.hud.resources.military_caption;
import games.generalszh.presentation.hud.algorithms.control_bar_timers;
import engine.gameplay.rts.construction.components.sale;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.construction.components.construction_progress;
import games.generalszh.presentation.hud.algorithms.named_timer_lines;

// The extract stage of the interface's drawing: what InGameUI::postDraw and each Drawable's UI (drawHealthBar,
// drawAmmo, drawIconUI) would draw this frame, read from the presentation's resources and side tables and the
// simulation's components into the frame's InGameOverlay. Free functions, one per part of the overlay.
export namespace generalszh::presentation
{
// What the overlay is read with besides the world: InGameUI.ini, Animation2D.ini, what the scripts hide, the zoom.
struct OverlaySource
{
	const content::InGameUiContent *inGameUi{nullptr};
	const content::Anim2DTemplates *animations{nullptr};
	bool drawIconUi{true};                  // a script's DISPLAY_ICON_UI (off: no icons or veterancy)
	bool specialPowerDisplayDisabled{false}; // a script hid the superweapon countdowns
	float zoom{1};                          // the camera's zoom
	std::u16string constructionPattern;     // CONTROLBAR:UnderConstructionDesc ("Building: %.0f%%")
};

namespace overlay_detail
{
inline std::array<float, 4> Rgba(const std::array<std::uint8_t, 4> &c)
{
	return {c[0] / 255.0f, c[1] / 255.0f, c[2] / 255.0f, c[3] / 255.0f};
}
}

// InGameUI::postDraw's messages: newest first in the slots, oldest first on screen, from MessagePosition.
inline void ExtractMessages(ecs::World &world, const OverlaySource &source, InGameOverlay &overlay)
{
	const auto *messages = world.FindResource<InGameMessages>();
	if (messages == nullptr)
		return;
	overlay.messageAt = {static_cast<float>(source.inGameUi->messagePosition[0]), static_cast<float>(source.inGameUi->messagePosition[1])};
	for (std::size_t index = messages->slots.size(); index-- > 0;)
		if (const auto &message = messages->slots[index]; message.shown)
			overlay.messages.push_back({message.text, overlay_detail::Rgba(message.color)});
}

// InGameUI's named timers: a line each, the flash colour on its flash frames.
inline void ExtractNamedTimers(ecs::World &world, session::SessionView &game, const OverlaySource &source, InGameOverlay &overlay)
{
	const auto *timers = world.FindResource<NamedTimers>();
	if (timers == nullptr)
		return;
	const content::InGameUiContent &ui = *source.inGameUi;
	for (const auto &line : NamedTimerLines(*timers, [&game](std::string_view name) { return game.ScriptCounter(name); }, game.CurrentTick()))
		overlay.namedTimers.push_back({line.text, line.ready, overlay_detail::Rgba(line.flashColor ? ui.namedTimerFlashColor : ui.namedTimerNormalColor)});
	overlay.namedTimerAt = {Engine::Math::ToFloat(ui.namedTimerPosition[0]), Engine::Math::ToFloat(ui.namedTimerPosition[1])};
}

// The script's screen fade and the military caption as typed so far.
inline void ExtractFadeAndCaption(ecs::World &world, const OverlaySource &source, InGameOverlay &overlay)
{
	if (const auto *fade = world.FindResource<ScreenFade>())
	{
		overlay.fade = static_cast<std::uint8_t>(fade->kind);
		overlay.fadeValue = fade->value;
	}
	if (const auto *caption = world.FindResource<MilitaryCaption>(); caption != nullptr && caption->shown)
	{
		overlay.caption.shown = true;
		overlay.caption.lines = caption->lines;
		overlay.caption.color = overlay_detail::Rgba(caption->color);
		overlay.caption.block = caption->blockDrawn;
		overlay.caption.at = {static_cast<float>(source.inGameUi->militaryCaptionPosition[0]), static_cast<float>(source.inGameUi->militaryCaptionPosition[1])};
	}
}

// Drawable::drawAmmo for its own player's selected: the first of its weapons that ShowsAmmoPips, a pip a round of its
// clip, full while loaded (getRemainingAmmo), left-aligned with its health bar under its projected top.
inline void ExtractAmmoPips(ecs::World &world, session::SessionView &game, ecs::Entity entity, const SelectionLook &look, float sx, float sy,
	float zoom, InGameOverlay &overlay)
{
	const auto *local = world.FindResource<LocalPlayer>();
	const auto *owner = world.Get<engine::gameplay::Owner>(entity);
	const auto *armament = world.Get<engine::gameplay::Armament>(entity);
	if (local == nullptr || !local->valid || owner == nullptr || owner->player != local->player || armament == nullptr)
		return;
	const auto *slots = world.Get<engine::gameplay::WeaponSlots>(entity);
	const std::uint64_t tick = game.CurrentTick();
	std::optional<std::pair<std::uint32_t, std::uint32_t>> pips;
	for (std::size_t slot = 0; slot < engine::gameplay::WeaponSlotCount && !pips; ++slot)
	{
		const bool current = slots == nullptr ? slot == 0 : slot == slots->current;
		if (slots == nullptr && slot != 0)
			break;
		const std::uint32_t id = current ? armament->weapon : slots->slots[slot].weapon;
		const auto *content = id == engine::gameplay::WeaponCatalog::None ? nullptr : game.WeaponContentOf(id);
		if (content == nullptr || !content->showsAmmoPips)
			continue;
		const std::uint32_t clipSize = content->simulation.clipSize;
		pips = std::pair{clipSize, current ? engine::gameplay::RemainingAmmo(clipSize, armament->clip, armament->readyTick, armament->reloading, tick)
			: engine::gameplay::RemainingAmmo(clipSize, slots->slots[slot].clip, slots->slots[slot].readyTick, slots->slots[slot].reloading, tick)};
	}
	if (!pips || pips->first == 0)
		return;
	const auto *transform = world.Get<engine::gameplay::Transform>(entity);
	const auto *definition = world.Get<engine::gameplay::DefinitionRef>(entity);
	const auto &data = game.Content().gameData;
	const auto &view = world.Resource<InteractionView>();
	float cx = 0, cy = 0;
	const auto &offset = data.ammoPipWorldOffset;
	if (!view.Project(Engine::Math::ToFloat(transform->position.x + offset[0]), Engine::Math::ToFloat(transform->position.y + offset[1]),
			Engine::Math::ToFloat(transform->position.z + offset[2]) + look.top, cx, cy))
		return;
	const auto region = HealthRegion(static_cast<int>(sx), static_cast<int>(sy), look.healthBoxWidth, zoom);
	const float bounding = Engine::Math::ToFloat(content::BoundingSphereRadius(game.Definition(definition->index).geometry));
	for (std::uint32_t pip = 0; pip < pips->first; ++pip)
	{
		OverlayImage image{pip < pips->second ? "SCPAmmoFull" : "SCPAmmoEmpty", 0.0f, 0.0f, 1.0f, 1.0f};
		image.placement = OverlayImage::Placement::AmmoPip;
		image.region = region;
		image.pip = static_cast<int>(pip);
		image.pipCenterY = static_cast<int>(cy);
		image.pipOffset = Engine::Math::ToFloat(data.ammoPipScreenOffset[1]);
		image.pipBounding = bounding;
		overlay.images.push_back(std::move(image));
	}
}

// Drawable::drawHealthBar for the selected (computeHealthRegion: the health box position projected, its width over the
// zoom, 3 high, starting 0.45 of its width left of centre; none at no health or for FORCEATTACKABLE), then its ammo.
inline void ExtractSelectedMarkers(ecs::World &world, session::SessionView &game, const OverlaySource &source, InGameOverlay &overlay)
{
	const auto &view = world.Resource<InteractionView>();
	const auto &catalog = world.Resource<SelectionCatalog>();
	const auto &data = game.Content().gameData;
	const float zoom = std::max(source.zoom, 0.01f);
	for (const ecs::Entity entity : world.Side<Selected>().Entities())
	{
		const auto *transform = world.Get<engine::gameplay::Transform>(entity);
		const auto *definition = world.Get<engine::gameplay::DefinitionRef>(entity);
		const auto *body = world.Get<engine::gameplay::Health>(entity);
		if (transform == nullptr || definition == nullptr || body == nullptr || body->maximum <= Engine::Math::Fixed{} || body->current <= Engine::Math::Fixed{})
			continue;
		const SelectionLook *look = catalog.Of(definition->index);
		if (look == nullptr || look->healthBoxWidth <= 0.0f || (look->kinds & select_kind::ForceAttackable) != 0)
			continue;
		float sx = 0, sy = 0;
		if (!view.Project(Engine::Math::ToFloat(transform->position.x), Engine::Math::ToFloat(transform->position.y),
				Engine::Math::ToFloat(transform->position.z) + look->top + 10.0f, sx, sy))
			continue;
		const float width = look->healthBoxWidth / zoom;
		SelectedMarker marker;
		marker.x = std::floor(sx - width * 0.45f);
		marker.y = std::floor(sy - 1.5f);
		marker.width = std::floor(width);
		marker.health = std::clamp(Engine::Math::ToFloat(body->current) / Engine::Math::ToFloat(body->maximum), 0.0f, 1.0f);
		marker.reallyDamaged = body->current < body->maximum * data.unitReallyDamaged;
		marker.damaged = !marker.reallyDamaged && body->current < body->maximum * data.unitDamaged;
		if (const auto *off = world.Get<engine::gameplay::Disabled>(entity))
			marker.disabled = (off->mask & ~engine::gameplay::disabled_type::Held) != 0;
		overlay.selected.push_back(marker);
		ExtractAmmoPips(world, game, entity, *look, sx, sy, zoom, overlay);
	}
}

// Drawable::drawIconUI for everything seen, while icon UI is on and no script fade runs: its animated icons as
// ObjectIconSystem left them (their Animation2D's image this many logic frames after it was made) against its health
// region, then its veterancy (alive, not IGNORED_IN_GUI, with a health box).
inline void ExtractObjectIcons(ecs::World &world, const OverlaySource &source, InGameOverlay &overlay)
{
	if (!source.drawIconUi || overlay.fade != 0)
		return;
	const auto &view = world.Resource<InteractionView>();
	const auto &catalog = world.Resource<SelectionCatalog>();
	const float zoom = std::max(source.zoom, 0.01f);
	const double clock = world.Resource<PresentationFrame>().clock;
	const auto *looks = world.FindResource<LookCatalog>();
	const auto &iconTable = world.Side<ObjectIcons>();
	const content::Anim2DTemplates &animations = *source.animations;
	world.Resource<engine::gameplay::VisibleObjects>().ForEach([&](const engine::gameplay::VisibleObject &object) {
		const SelectionLook *look = catalog.Of(object.definition);
		// drawConstructPercent (health region or not): a structure under construction and not being sold shows "Building:
		// n%" in white, centred over its geometry's centre, unless that lands off the screen's left edge.
		if (const auto *progress = world.Get<engine::gameplay::ConstructionProgress>(object.entity);
			progress != nullptr && !source.constructionPattern.empty() && world.Has<engine::gameplay::UnderConstruction>(object.entity) &&
			!world.Has<engine::gameplay::Sale>(object.entity))
		{
			const auto &at = object.transform.position;
			const float centre = look != nullptr ? look->center : 0.0f; // getZDeltaToCenterPosition
			float sx = 0, sy = 0;
			if (view.Project(Engine::Math::ToFloat(at.x), Engine::Math::ToFloat(at.y), Engine::Math::ToFloat(at.z) + centre, sx, sy) && static_cast<int>(sx) >= 1)
				overlay.texts.push_back({FormatConstructionPercent(source.constructionPattern, progress->percent.Raw()), sx, sy, {1, 1, 1, 1}});
		}
		if (look == nullptr || look->healthBoxWidth <= 0.0f)
			return;
		const auto &at = object.transform.position;
		float sx = 0, sy = 0;
		if (!view.Project(Engine::Math::ToFloat(at.x), Engine::Math::ToFloat(at.y), Engine::Math::ToFloat(at.z) + look->top + 10.0f, sx, sy))
			return;
		const int screenX = static_cast<int>(sx), screenY = static_cast<int>(sy);
		const DefinitionLooks *kind = looks != nullptr ? looks->Of(object.definition) : nullptr;
		if (const ObjectIcons *icons = iconTable.Get(object.entity); icons != nullptr && icons->drawn != 0)
		{
			const float scale = EnthusiasticScale(kind != nullptr && (kind->structure || kind->hugeVehicle), kind != nullptr && kind->vehicle);
			for (std::size_t index = 0; index < ObjectIconCount; ++index)
			{
				const auto icon = static_cast<ObjectIcon>(index);
				if (!icons->Drawn(icon))
					continue;
				const auto found = animations.find(ObjectIconAnimations[index]);
				if (found == animations.end() || found->second.images.empty())
					continue;
				const auto frames = static_cast<std::uint64_t>(std::max(clock - icons->since[index], 0.0) * 30.0);
				OverlayImage image{found->second.images[found->second.ImageAt(frames)], sx, sy, 1.0f, 1.0f};
				image.placement = OverlayImage::Placement::Icon;
				image.icon = icon;
				image.region = HealthRegion(screenX, screenY, look->healthBoxWidth, zoom);
				image.iconScale = scale;
				overlay.images.push_back(std::move(image));
			}
		}
		const auto *experience = world.Get<engine::gameplay::Experience>(object.entity);
		const auto *body = world.Get<engine::gameplay::Health>(object.entity);
		if (experience == nullptr || (body != nullptr && body->current <= Engine::Math::Fixed{}) || world.Has<engine::gameplay::Dying>(object.entity) ||
			(kind != nullptr && kind->ignoredInGui))
			return;
		if (const std::string_view veteran = VeterancyImage(experience->level); !veteran.empty())
		{
			OverlayImage image{std::string(veteran), static_cast<float>(screenX), static_cast<float>(screenY), 1.0f, 1.0f};
			image.placement = OverlayImage::Placement::Veterancy;
			image.healthBoxWidth = look->healthBoxWidth;
			image.zoom = zoom;
			overlay.images.push_back(std::move(image));
		}
	});
}

// InGameUI::updateAndDrawWorldAnimations: each at its risen point, on the image its Animation2D shows this many logic
// frames in, its own size at 1.3 over the zoom, at its fading alpha.
inline void ExtractWorldAnimations(ecs::World &world, const OverlaySource &source, InGameOverlay &overlay)
{
	const auto *shown = world.FindResource<WorldAnimations>();
	if (shown == nullptr)
		return;
	const auto &view = world.Resource<InteractionView>();
	const float zoom = std::max(source.zoom, 0.01f);
	const double clock = world.Resource<PresentationFrame>().clock;
	for (const WorldAnimation &animation : shown->shown)
	{
		const auto found = source.animations->find(animation.animation);
		if (found == source.animations->end() || found->second.images.empty())
			continue;
		const auto at = animation.PositionAt(clock);
		float sx = 0, sy = 0;
		if (!view.Project(at[0], at[1], at[2], sx, sy))
			continue;
		const auto frames = static_cast<std::uint64_t>(std::max(clock - animation.start, 0.0) * 30.0);
		overlay.images.push_back({found->second.images[found->second.ImageAt(frames)], sx, sy, 1.3f / zoom, animation.AlphaAt(clock)});
	}
}

// InGameUI::postDraw's superweapon countdowns: in their owner's colour, a ready one flashing (unless a script hid them).
inline void ExtractSuperweapons(ecs::World &world, session::SessionView &game, const OverlaySource &source, InGameOverlay &overlay)
{
	const content::InGameUiContent &ui = *source.inGameUi;
	overlay.superweaponAt = {Engine::Math::ToFloat(ui.superweaponPosition[0]), Engine::Math::ToFloat(ui.superweaponPosition[1])};
	auto *flashing = world.FindResource<hud::SuperweaponFlash>();
	if (source.specialPowerDisplayDisabled || flashing == nullptr)
		return;
	const auto *looks = world.FindResource<LookCatalog>();
	const std::uint64_t tick = game.CurrentTick();
	for (const hud::SuperweaponEntry &entry : hud::ReadSuperweaponTimers(game, tick))
	{
		OverlaySuperweapon line{entry.shown, entry.power, hud::CountdownText(entry.readySeconds), {1, 1, 1, 1}, entry.ready};
		if (looks != nullptr)
			line.color = looks->ColorOf(entry.player);
		line.color[3] = 1.0f;
		if (entry.shown && entry.ready && flashing->FlashColor(tick, ui.superweaponFlashFrames))
			line.color = overlay_detail::Rgba(ui.superweaponFlashColor);
		overlay.superweapons.push_back(std::move(line));
	}
}

// InGameUI::drawFloatingText: at its start, risen a pixel a tick, in its colour at its alpha.
inline void ExtractFloatingTexts(ecs::World &world, InGameOverlay &overlay)
{
	const auto &view = world.Resource<InteractionView>();
	const auto &settings = world.Resource<FloatingTextSettings>();
	for (const FloatingText &text : world.Resource<FloatingTexts>().shown)
	{
		float sx = 0, sy = 0;
		if (!view.Project(text.at[0], text.at[1], text.at[2], sx, sy))
			continue;
		overlay.texts.push_back({text.text, sx, sy - static_cast<float>(text.ticks) * settings.riseRate,
			{text.color[0], text.color[1], text.color[2], static_cast<float>(std::clamp(text.alpha, 0, 255)) / 255.0f}});
	}
}

// The whole overlay, in the order InGameUI draws it.
inline InGameOverlay ExtractInGameOverlay(ecs::World &world, session::SessionView &game, const OverlaySource &source)
{
	InGameOverlay overlay;
	const auto &box = world.Resource<SelectionBox>();
	overlay.boxActive = box.active;
	ExtractMessages(world, source, overlay);
	ExtractNamedTimers(world, game, source, overlay);
	ExtractFadeAndCaption(world, source, overlay);
	overlay.box = {std::min(box.x0, box.x1), std::min(box.y0, box.y1), std::max(box.x0, box.x1), std::max(box.y0, box.y1)};
	ExtractSelectedMarkers(world, game, source, overlay);
	ExtractObjectIcons(world, source, overlay);
	ExtractWorldAnimations(world, source, overlay);
	ExtractSuperweapons(world, game, source, overlay);
	ExtractFloatingTexts(world, overlay);
	return overlay;
}
}
