export module games.generalszh.hud.control_bar_portrait;
import std;

export import games.generalszh.session.session_view;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.components.team_member;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.rts.teams.resources.team_roster;
import engine.gameplay.rts.stealth.components.stealth;
import engine.gameplay.rts.upgrades.components.upgradable;
import engine.gameplay.rts.upgrades.resources.player_upgrades;
import engine.gameplay.rts.production.components.production_queue;
import engine.gameplay.rts.construction.components.sale;
import games.generalszh.hud.control_bar_state;

// The right HUD's portrait (WinUnitSelected: the selection's CameoWindow and its UnitUpgrade1..5), as ControlBar's
// contexts choose it (evaluateContextUI -> switchToContext / populateMultiSelect / the build queue) and
// ControlBar::setPortraitByObject fills it. Read from the session's view each frame, never kept.
export namespace generalszh::hud
{
inline constexpr std::size_t UpgradeCameos = 5; // MAX_UPGRADE_CAMEO_UPGRADES

struct UpgradeCameo
{
	bool shown{false};
	std::string image;   // the upgrade's ButtonImage
	bool enabled{false}; // the object has it, or its player has it complete
};

struct PortraitState
{
	bool shown{false};   // a portrait object (none: the right HUD shows its own image)
	std::string image;   // its SelectPortrait (getSelectedPortraitImage; empty: none)
	std::string overlay; // its rank over it (calculateVeterancyOverlayForObject)
	std::array<UpgradeCameo, UpgradeCameos> upgrades{};
};

namespace portrait_detail
{
// StealthUpdate::calcStealthedStatusForPlayer's STEALTHLOOK_DISGUISED_ENEMY: a stealthed disguiser in its disguise, seen
// by a player its team is not allied to (an observer is everyone's ally).
inline bool DisguisedEnemy(const ecs::World &world, ecs::Entity entity, std::optional<std::uint32_t> viewer)
{
	namespace gp = engine::gameplay;
	const auto *stealth = world.Get<gp::Stealth>(entity);
	if (stealth == nullptr || !stealth->Option(gp::stealth_option::DisguisesAsTeam) || !stealth->Has(gp::stealth_flag::Stealthed) ||
		!stealth->Has(gp::stealth_flag::Disguised) || !viewer)
		return false;
	const auto *owner = world.Get<gp::Owner>(entity);
	const auto *member = world.Get<gp::TeamMember>(entity);
	const auto *relationships = world.FindResource<gp::Relationships>();
	const auto *roster = world.FindResource<gp::TeamRoster>();
	if (owner == nullptr || relationships == nullptr || roster == nullptr)
		return true;
	const auto viewerTeam = roster->DefaultTeam(*viewer);
	const std::uint32_t team = member != nullptr ? member->team : roster->DefaultTeam(owner->player).value_or(0);
	return !viewerTeam || !relationships->Allies(team, owner->player, *viewerTeam, *viewer);
}
}

// ControlBar::setPortraitByObject(obj), for `viewer` (the local player; none: an observer).
inline PortraitState PortraitOf(session::SessionView &view, std::optional<std::uint32_t> viewer, ecs::Entity entity)
{
	namespace gp = engine::gameplay;
	PortraitState portrait;
	const auto &world = view.World();
	const auto index = view.DefinitionOf(entity);
	const auto *owner = world.Get<gp::Owner>(entity);
	if (!index || owner == nullptr)
		return portrait;
	const content::ObjectDefinition *thing = &view.Definition(*index);
	// A civilian vehicle without a terrorist in it: no portrait unless its own player looks.
	if (thing->Is("SHOW_PORTRAIT_WHEN_CONTROLLED") && (!viewer || owner->player != *viewer))
		return portrait;
	std::uint32_t player = owner->player;
	// An enemy's disguised bomb truck: the disguise's portrait, with the disguise's player's upgrades.
	if (portrait_detail::DisguisedEnemy(world, entity, viewer))
	{
		const auto *stealth = world.Get<gp::Stealth>(entity);
		if (stealth->shownAs != gp::Stealth::NoDisguise && stealth->shownAs < view.DefinitionCount())
			thing = &view.Definition(stealth->shownAs);
		if (thing->Is("SHOW_PORTRAIT_WHEN_CONTROLLED"))
			return portrait;
		if (stealth->Has(gp::stealth_flag::Disguised) && stealth->disguisePlayer >= 0)
			player = static_cast<std::uint32_t>(stealth->disguisePlayer);
	}
	portrait.shown = true;
	portrait.image = thing->selectPortrait;
	portrait.overlay = control_bar_detail::VeterancyOverlay(world, entity);
	const content::GameContent &content = view.Content();
	const auto *own = world.Get<gp::Upgradable>(entity);
	const auto *players = world.FindResource<gp::PlayerUpgrades>();
	for (std::size_t slot = 0; slot < UpgradeCameos; ++slot)
	{
		const std::string &name = thing->upgradeCameos[slot];
		const auto upgrade = name.empty() ? std::nullopt : content.upgrades.Find(name);
		if (!upgrade)
			continue;
		UpgradeCameo &cameo = portrait.upgrades[slot];
		cameo.shown = true;
		cameo.image = content.upgrades.upgrades[*upgrade].buttonImage;
		cameo.enabled = (own != nullptr && own->completed.Has(*upgrade)) || (players != nullptr && players->Completed(player).Has(*upgrade));
	}
	return portrait;
}

// The portrait the control bar shows for `selection` (evaluateContextUI): none for nothing; another's object (not
// controllable) shows its own; several show the first that is not IGNORED_IN_GUI or sold (populateMultiSelect: it keeps
// that object even when their portraits differ); one of the player's own shows its own unless its production queue has
// something in it (the queue shows in its place).
inline PortraitState ReadPortrait(session::SessionView &view, std::optional<std::uint32_t> viewer, std::span<const ecs::Entity> selection)
{
	namespace gp = engine::gameplay;
	const auto &world = view.World();
	if (selection.empty() || !world.IsAlive(selection.front()))
		return {};
	const ecs::Entity first = selection.front();
	const auto *owner = world.Get<gp::Owner>(first);
	// areSelectedObjectsControllable: the first one's controller is the local player.
	if (owner == nullptr || !viewer || owner->player != *viewer)
		return PortraitOf(view, viewer, first);
	if (selection.size() > 1)
	{
		for (const ecs::Entity each : selection)
		{
			const auto index = world.IsAlive(each) ? view.DefinitionOf(each) : std::nullopt;
			if (!index || view.Definition(*index).Is("IGNORED_IN_GUI") || world.Has<gp::Sale>(each))
				continue;
			return PortraitOf(view, viewer, each);
		}
		return {};
	}
	if (const auto *queue = world.Get<gp::ProductionQueue>(first); queue != nullptr && queue->count > 0)
		return {};
	return PortraitOf(view, viewer, first);
}
}
