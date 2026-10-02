export module games.generalszh.hud.control_bar_state;
import std;

export import games.generalszh.session.session_view;
import engine.gameplay.rts.economy.resources.player_money;
import engine.gameplay.rts.economy.resources.player_energy;
import engine.gameplay.rts.production.components.production_queue;
import engine.gameplay.common.identity.components.owner;
import engine.gameplay.common.identity.resources.relationships;
import engine.gameplay.rts.containment.components.transport;
import engine.gameplay.rts.containment.components.mount;
import engine.gameplay.rts.containment.resources.cargo_manifest;
import engine.gameplay.rts.veterancy.components.experience;
import engine.gameplay.common.status.components.disabled;
import engine.gameplay.rts.construction.components.sale;
import engine.gameplay.rts.construction.components.under_construction;
import engine.gameplay.rts.construction.components.construction_progress;
import games.generalszh.gameplay.creation.components.ocl_timer;
import games.generalszh.hud.idle_workers;
import games.generalszh.gameplay.orders.resources.command_bar_overrides;
import games.generalszh.hud.science_buttons;

// What the control bar shows this frame for the local player and their selection (the original's
// ControlBar::populateCommand / updateContextCommand / populateBuildQueue, the transport and structure inventories
// (doTransportInventoryUI, populateStructureInventory) and InGameUI's money): read from the session's view, never kept.
// Headless: the view model turns it into what the windows show.
export namespace generalszh::hud
{
inline constexpr std::size_t CommandButtons = 14; // ButtonCommand01..14 (slots 15..18 are script-only)
inline constexpr std::size_t QueueButtons = 9;    // ButtonQueue01..09 (MAX_BUILD_QUEUE_BUTTONS)

struct CommandSlot
{
	const content::CommandButtonContent *button{nullptr}; // none: the slot is empty
	gameplay::ButtonState state{gameplay::ButtonState::Hidden};
	std::string image; // the button's art (ButtonImage)
	std::uint32_t clock{1000}; // a charging power's inverse clock, per mille (1000: none)
	bool flashing{false};      // lit by a script's CAMEO_FLASH now (WIN_STATUS_FLASHING)
	// An inventory button's rider (GUI_COMMAND_EXIT_CONTAINER: ControlBar::m_containData); none: an empty slot.
	ecs::Entity rider{};
	// Drawn over its art (GadgetButtonDrawOverlayImage: a rider's veterancy chevron); empty: none.
	std::string overlay;
	// WIN_STATUS_ALWAYS_COLOR: disabled, it dims rather than greys (the structure inventory's buttons).
	bool alwaysColor{false};
};

// The windows of the structure inventory (ControlBarStructureInventory.cpp): the riders' buttons, then Stop and Evacuate.
inline constexpr std::size_t StructureInventoryButtons = 10; // MAX_STRUCTURE_INVENTORY_BUTTONS
inline constexpr std::size_t StructureStopSlot = 10;        // STOP_ID
inline constexpr std::size_t StructureEvacuateSlot = 11;    // EVACUATE_ID

// A container as the control bar sees it (ContainModuleInterface): whether it shows its riders (isDisplayedOnControlBar),
// whether it is garrisonable (isGarrisonable: a GarrisonContain), its room (getContainMax), the room its riders take
// beyond one each (getExtraSlotsInUse), and its riders in order (iterateContained forward: first in first; a tunnel
// network's one list: everyone in any of its tunnels, by when they got in).
struct ContainerInventory
{
	bool displayed{false};
	bool garrisonable{false};
	std::uint32_t max{0};
	std::uint32_t extraSlots{0};
	std::vector<ecs::Entity> riders;
};

struct QueueSlot
{
	bool unit{true};            // a unit (else an upgrade under research)
	std::string image;          // the unit's or upgrade's ButtonImage
	std::uint32_t progress{0};  // the front one's build clock, per mille (0..1000)
	std::uint32_t productionId{0};
	std::string upgrade;        // an upgrade's name (what cancels it)
};

// ControlBar::switchToContext's contexts as evaluateContextUI picks them for the selection.
enum class ControlBarContext : std::uint8_t
{
	None,
	Command,            // CB_CONTEXT_COMMAND: its command set (and production queue)
	MultiSelect,        // CB_CONTEXT_MULTI_SELECT
	StructureInventory, // CB_CONTEXT_STRUCTURE_INVENTORY
	UnderConstruction,  // CB_CONTEXT_UNDER_CONSTRUCTION: Cancel and "Building: n%"
	OclTimer,           // CB_CONTEXT_OCL_TIMER: an OCLUpdate's countdown, with Sell (or a tech building's rally point)
};

struct ControlBarState
{
	bool hasPlayer{false};
	ControlBarContext context{ControlBarContext::None};
	// The under-construction or OCL timer panel's button (Command_CancelConstruction, Command_Sell,
	// Command_SetRallyPoint; none: hidden).
	const content::CommandButtonContent *contextButton{nullptr};
	std::int64_t constructionPercent{0}; // Object::getConstructionPercent, 16.16 fixed point
	std::uint64_t oclRemaining{0};       // OCLUpdate::getRemainingFrames
	std::uint64_t oclTotal{0};           // its timer's whole length (next - started)
	std::size_t idleWorkers{0};          // InGameUI::getIdleWorkerCount
	std::int64_t money{0};
	std::int32_t powerProduced{0};
	std::int32_t powerConsumed{0};
	ecs::Entity selected{};                    // the object whose commands show (several: the first that counts)
	// What its commands go to: the one selected object, or the selected objects that count (populateMultiSelect: not
	// IGNORED_IN_GUI, not being sold), in the selection's order.
	std::vector<ecs::Entity> group;
	bool multiSelect{false};
	std::array<CommandSlot, CommandButtons> slots{};
	std::vector<QueueSlot> queue;             // its production, front first
};

namespace control_bar_detail
{
inline bool HasModule(const content::ObjectDefinition &object, std::string_view type)
{
	return std::any_of(object.modules.begin(), object.modules.end(), [&](const content::ModuleEntry &module) { return module.type == type; });
}

// isDisplayedOnControlBar: TransportContain and its kin, GarrisonContain and TunnelContain say yes; OpenContain's others
// (heal, parachute, cave, mob nexus) no; an OverlordContain asks the structure it carries once it redirects to it
// (getRedirectedContain: a Battle Bunker on top), else no.
inline bool DisplaysContents(session::SessionView &view, ecs::Entity entity, bool redirected = false)
{
	namespace gp = engine::gameplay;
	const auto index = view.DefinitionOf(entity);
	if (!index)
		return false;
	const content::ObjectDefinition &object = view.Definition(*index);
	for (const std::string_view type : {"TransportContain", "HelixContain", "InternetHackContain", "RailedTransportContain", "RiderChangeContain",
			 "GarrisonContain", "TunnelContain"})
		if (HasModule(object, type))
			return true;
	if (!redirected && HasModule(object, "OverlordContain"))
	{
		const auto &world = view.World();
		const gp::Mount *mount = world.Get<gp::Mount>(entity);
		return mount != nullptr && world.IsAlive(mount->rider) && world.Has<gp::Transport>(mount->rider) && DisplaysContents(view, mount->rider, true);
	}
	return false;
}

// calculateVeterancyOverlayForObject: the rider's rank chevron (ControlBar::init: SSChevron1L / 2L / 3L); a regular: none.
inline std::string VeterancyOverlay(const ecs::World &world, ecs::Entity entity)
{
	const auto *experience = world.Get<engine::gameplay::Experience>(entity);
	switch (experience != nullptr ? experience->level : 0)
	{
	case 1: return "SSChevron1L";
	case 2: return "SSChevron2L";
	case 3: return "SSChevron3L";
	default: return {};
	}
}

// A rider's button: its ButtonImage, its rank over it (populateInvDataCallback / populateButtonProc).
inline void ShowRider(session::SessionView &view, CommandSlot &slot, ecs::Entity rider)
{
	slot.rider = rider;
	if (const auto index = view.DefinitionOf(rider))
		slot.image = view.Definition(*index).buttonImage;
	slot.overlay = VeterancyOverlay(view.World(), rider);
}
}

// The container's inventory (none without a Transport).
inline ContainerInventory ReadInventory(session::SessionView &view, ecs::Entity entity)
{
	namespace gp = engine::gameplay;
	ContainerInventory inventory;
	const auto &world = view.World();
	const gp::Transport *transport = world.IsAlive(entity) ? world.Get<gp::Transport>(entity) : nullptr;
	if (transport == nullptr)
		return inventory;
	inventory.displayed = control_bar_detail::DisplaysContents(view, entity);
	if (const auto index = view.DefinitionOf(entity))
		inventory.garrisonable = control_bar_detail::HasModule(view.Definition(*index), "GarrisonContain");
	inventory.max = transport->definition.slots;
	const gp::CargoManifest &manifest = world.Resource<gp::CargoManifest>();
	if (const std::optional<std::uint32_t> network = manifest.NetworkOf(entity))
	{
		// TunnelContain::iterateContained: the player's TunnelTracker's one list, in the order they went in.
		for (const ecs::Entity tunnel : manifest.Network(*network))
			for (const ecs::Entity rider : manifest.Aboard(tunnel))
				inventory.riders.push_back(rider);
		std::stable_sort(inventory.riders.begin(), inventory.riders.end(), [&](ecs::Entity left, ecs::Entity right) {
			const auto *a = world.Get<gp::Passenger>(left);
			const auto *b = world.Get<gp::Passenger>(right);
			return (a != nullptr ? a->since : 0) < (b != nullptr ? b->since : 0);
		});
		return inventory;
	}
	const auto aboard = manifest.Aboard(entity);
	inventory.riders.assign(aboard.begin(), aboard.end());
	const auto count = static_cast<std::uint32_t>(inventory.riders.size());
	inventory.extraSlots = transport->occupied > count ? transport->occupied - count : 0u;
	return inventory;
}

// CB_CONTEXT_MULTI_SELECT (ControlBar::populateMultiSelect / addCommonCommands, then updateContextMultiSelect), the
// player's own objects selected (areSelectedObjectsControllable: the first one's): the command windows show the commands
// common to the selected objects that count (not IGNORED_IN_GUI, not being sold), slot by slot: the first one's
// OK_FOR_MULTI_SELECT buttons; each next one keeps a slot only where its button is the same (an ATTACK_MOVE from any of
// them takes an empty slot, and keeps it); one without a command set empties every slot. Then each frame each object's
// availability of each shown button: hidden for any hides it; the slot is enabled only while at least one can do it
// (available or active), else it shows as the last one had it (restricted, not ready). No production queue shows.
inline void ReadMultiSelect(session::SessionView &view, ControlBarState &state, std::span<const ecs::Entity> selection)
{
	namespace gp = engine::gameplay;
	auto &world = view.World();
	const content::GameContent &content = view.Content();
	state.multiSelect = true;
	std::array<const content::CommandButtonContent *, CommandButtons> common{};
	std::array<bool, CommandButtons> shown{};
	bool first = true;
	for (const ecs::Entity each : selection)
	{
		const auto index = world.IsAlive(each) ? view.DefinitionOf(each) : std::nullopt;
		if (!index || view.Definition(*index).Is("IGNORED_IN_GUI") || world.Has<gp::Sale>(each))
			continue;
		state.group.push_back(each);
		const auto set = generalszh::gameplay::EffectiveCommandSet(content.commands, world.FindResource<generalszh::gameplay::CommandBarOverrides>(),
			view.CommandSetOf(each));
		if (!set)
		{
			common.fill(nullptr);
			shown.fill(false);
			first = false;
			continue;
		}
		for (std::size_t slot = 0; slot < CommandButtons; ++slot)
		{
			const content::CommandButtonContent *button = content.commands.Button(set->buttons[slot]);
			if (first)
			{
				if (button != nullptr && (button->options & content::button_option::OkForMultiSelect) != 0)
				{
					common[slot] = button;
					shown[slot] = true;
				}
				continue;
			}
			const bool attackMove = (button != nullptr && button->commandName == "ATTACK_MOVE") || (common[slot] != nullptr && common[slot]->commandName == "ATTACK_MOVE");
			if (attackMove && common[slot] == nullptr)
			{
				common[slot] = button;
				shown[slot] = true;
			}
			else if (button != common[slot] && !attackMove)
			{
				common[slot] = nullptr;
				shown[slot] = false;
			}
		}
		first = false;
	}
	if (state.group.empty())
		return;
	state.selected = state.group.front();
	std::array<int, CommandButtons> able{};
	std::array<gameplay::ButtonState, CommandButtons> last{};
	for (const ecs::Entity each : state.group)
		for (std::size_t slot = 0; slot < CommandButtons; ++slot)
		{
			if (!shown[slot] || common[slot] == nullptr)
				continue;
			const gameplay::ButtonState availability = view.CommandAvailability(each, *common[slot]);
			last[slot] = availability;
			if (availability == gameplay::ButtonState::Hidden)
				shown[slot] = false;
			else if (availability == gameplay::ButtonState::Available || availability == gameplay::ButtonState::Active)
				++able[slot];
		}
	for (std::size_t slot = 0; slot < CommandButtons; ++slot)
	{
		if (!shown[slot] || common[slot] == nullptr)
			continue;
		CommandSlot &shownSlot = state.slots[slot];
		shownSlot.button = common[slot];
		shownSlot.image = common[slot]->buttonImage;
		shownSlot.state = able[slot] > 0 ? (last[slot] == gameplay::ButtonState::Active ? gameplay::ButtonState::Active : gameplay::ButtonState::Available)
			: (last[slot] == gameplay::ButtonState::Available || last[slot] == gameplay::ButtonState::Active ? gameplay::ButtonState::Restricted : last[slot]);
	}
}

// The control bar for `player` (none: an observer) and what they have selected: the commands of the one object of
// theirs selected (several: none yet), each button's availability, a container's riders on its inventory buttons, and
// its production queue; a garrisonable structure without a command set (theirs, or one they are neutral to) shows the
// structure inventory instead.
inline ControlBarState ReadControlBar(session::SessionView &view, std::optional<std::uint32_t> player, std::span<const ecs::Entity> selection)
{
	namespace gp = engine::gameplay;
	ControlBarState state;
	if (!player)
		return state;
	auto &world = view.World();
	state.hasPlayer = true;
	state.money = world.Resource<gp::PlayerMoney>().Balance(*player);
	const auto &energy = world.Resource<gp::PlayerEnergy>();
	state.powerProduced = static_cast<std::int32_t>(energy.Production(*player));
	state.powerConsumed = static_cast<std::int32_t>(energy.Consumption(*player));
	state.idleWorkers = IdleWorkers(view, *player).size();
	if (selection.empty() || !world.IsAlive(selection.front()))
		return state;
	if (selection.size() > 1)
	{
		// Another's objects selected: nothing (evaluateContextUI: not controllable).
		if (const auto *first = world.Get<gp::Owner>(selection.front()); first != nullptr && first->player == *player)
		{
			ReadMultiSelect(view, state, selection);
			if (!state.group.empty())
				state.context = ControlBarContext::MultiSelect;
		}
		return state;
	}
	const ecs::Entity selected = selection.front();
	const auto *owner = world.Get<gp::Owner>(selected);
	if (owner == nullptr)
		return state;
	const ContainerInventory inventory = ReadInventory(view, selected);
	// ControlBar::evaluateContextUI: another's object shows nothing, but for a garrisonable structure the local player is
	// NEUTRAL to (a civilian building: its inventory shows).
	if (owner->player != *player)
	{
		const auto *relationships = world.FindResource<gp::Relationships>();
		if (!inventory.garrisonable || inventory.max == 0 || relationships == nullptr ||
			relationships->Between(*player, owner->player) != gp::Relationship::Neutral)
			return state;
	}
	// "we show no interface for objects being sold".
	if (world.Has<gp::Sale>(selected))
		return state;
	state.selected = selected;
	state.group = {selected};
	const content::GameContent &content = view.Content();
	// Under construction comes before anything else (populateUnderConstruction): Command_CancelConstruction and its percent.
	if (owner->player == *player && world.Has<gp::UnderConstruction>(selected))
	{
		state.context = ControlBarContext::UnderConstruction;
		state.contextButton = content.commands.Button("Command_CancelConstruction");
		if (const auto *progress = world.Get<gp::ConstructionProgress>(selected))
			state.constructionPercent = progress->percent.Raw();
		return state;
	}
	// CB_CONTEXT_STRUCTURE_INVENTORY (a garrisonable object without a command set: populateStructureInventory): ten rider
	// buttons (Command_StructureExit), shown disabled and dimmed (ALWAYS_COLOR) up to its room and hidden past it, each
	// rider on the next with its art and rank, enabled; Stop and Evacuate beside them, enabled while anyone is inside.
	// updateContextStructureInventory repopulates them as the count changes: read afresh each frame here.
	if (inventory.garrisonable && view.CommandSetOf(selected).empty())
	{
		state.context = ControlBarContext::StructureInventory;
		const content::CommandButtonContent *exit = content.commands.Button("Command_StructureExit");
		for (std::size_t slot = 0; slot < StructureInventoryButtons && exit != nullptr; ++slot)
		{
			CommandSlot &shown = state.slots[slot];
			shown.button = exit;
			shown.image = exit->buttonImage;
			shown.alwaysColor = true;
			shown.state = slot + 1 > inventory.max ? gameplay::ButtonState::Hidden : gameplay::ButtonState::Restricted;
		}
		for (std::size_t index = 0; index < inventory.riders.size() && index < StructureInventoryButtons && exit != nullptr; ++index)
		{
			CommandSlot &shown = state.slots[index];
			control_bar_detail::ShowRider(view, shown, inventory.riders[index]);
			if (shown.state != gameplay::ButtonState::Hidden)
				shown.state = gameplay::ButtonState::Available;
		}
		const gameplay::ButtonState anyone = inventory.riders.empty() ? gameplay::ButtonState::Restricted : gameplay::ButtonState::Available;
		for (const auto &[slot, name] : std::array<std::pair<std::size_t, std::string_view>, 2>{{{StructureStopSlot, "Command_Stop"}, {StructureEvacuateSlot, "Command_Evacuate"}}})
			if (const content::CommandButtonContent *button = content.commands.Button(name))
			{
				state.slots[slot].button = button;
				state.slots[slot].image = button->buttonImage;
				state.slots[slot].state = anyone;
			}
		return state;
	}
	// An OCLUpdate (the Supply Drop Zone, the Reinforcement Pad): populateOCLTimer, Sell for anything but a tech building,
	// Command_SetRallyPoint for an AUTO_RALLYPOINT tech building, else no button; its countdown.
	if (const auto *timer = world.Get<generalszh::gameplay::OclTimer>(selected))
	{
		state.context = ControlBarContext::OclTimer;
		const auto index = view.DefinitionOf(selected);
		const bool tech = index && view.Definition(*index).Is("TECH_BUILDING");
		if (!tech)
			state.contextButton = content.commands.Button("Command_Sell");
		else if (view.Definition(*index).Is("AUTO_RALLYPOINT"))
			state.contextButton = content.commands.Button("Command_SetRallyPoint");
		const std::uint64_t now = view.CurrentTick();
		state.oclRemaining = timer->nextTick > now ? timer->nextTick - now : 0;
		state.oclTotal = timer->nextTick > timer->startedTick ? timer->nextTick - timer->startedTick : 0;
		return state;
	}
	if (!view.CommandSetOf(selected).empty())
		state.context = ControlBarContext::Command;
	// CommandSet::getCommandButton: the scripts' overrides first.
	const auto effective = generalszh::gameplay::EffectiveCommandSet(content.commands, world.FindResource<generalszh::gameplay::CommandBarOverrides>(),
		view.CommandSetOf(selected));
	if (const content::CommandSetContent *set = effective ? &*effective : nullptr)
		for (std::size_t slot = 0; slot < CommandButtons; ++slot)
		{
			const content::CommandButtonContent *button = content.commands.Button(set->buttons[slot]);
			if (button == nullptr)
				continue;
			CommandSlot &shown = state.slots[slot];
			shown.button = button;
			shown.state = view.CommandAvailability(selected, *button);
			if (shown.state == gameplay::ButtonState::NotReady)
				shown.clock = view.CommandClock(selected, *button);
			// A general's power not earned is hidden; earned, it shows its best rank's art.
			const auto art = ScienceButtonImage(view, *player, *button);
			if (!art)
				shown.state = gameplay::ButtonState::Hidden;
			shown.image = art.value_or(button->buttonImage);
		}
	// doTransportInventoryUI (a container that shows its contents): its EXIT_CONTAINER buttons, taken to run on from the
	// first to the last, show (disabled, no rank) up to its room less the extra room its riders take and hide past it, or
	// all hide on an unmanned vehicle; its riders fill the windows from the first on (populateInvDataCallback: art, rank).
	// updateContextCommand then sets each shown one by getCommandAvailability, as any command (an empty slot too). A
	// container that does not show its contents leaves them unshown.
	if (effective)
	{
		const auto isExit = [&](std::size_t slot) { return state.slots[slot].button != nullptr && state.slots[slot].button->commandName == "EXIT_CONTAINER"; };
		std::optional<std::size_t> first, last;
		std::uint32_t counted = 0;
		const auto *off = world.Get<gp::Disabled>(selected);
		const bool unmanned = off != nullptr && (off->mask & gp::disabled_type::Unmanned) != 0;
		const std::uint32_t room = inventory.max > inventory.extraSlots ? inventory.max - inventory.extraSlots : 0u;
		for (std::size_t slot = 0; slot < CommandButtons; ++slot)
		{
			if (!isExit(slot))
				continue;
			CommandSlot &shown = state.slots[slot];
			if (!inventory.displayed)
			{
				shown.state = gameplay::ButtonState::Hidden;
				continue;
			}
			if (!first)
				first = slot;
			last = slot;
			++counted;
			if (unmanned || counted > room)
				shown.state = gameplay::ButtonState::Hidden;
		}
		for (std::size_t index = 0; first && index < inventory.riders.size() && *first + index <= *last; ++index)
			if (isExit(*first + index))
				control_bar_detail::ShowRider(view, state.slots[*first + index], inventory.riders[index]);
	}
	if (const auto *queue = world.Get<gp::ProductionQueue>(selected))
		for (std::uint32_t index = 0; index < queue->count && index < QueueButtons; ++index)
		{
			const gp::ProductionEntry &entry = queue->entries[index];
			QueueSlot slot;
			slot.unit = entry.kind == gp::ProductionKind::Unit;
			if (slot.unit)
			{
				if (entry.definition < view.DefinitionCount())
					slot.image = view.Definition(entry.definition).buttonImage;
				slot.productionId = entry.productionId;
			}
			else if (entry.definition < content.upgrades.upgrades.size())
			{
				slot.image = content.upgrades.upgrades[entry.definition].buttonImage;
				slot.upgrade = content.upgrades.upgrades[entry.definition].name;
			}
			// m_percentComplete: its updates over calcTimeToBuild as its last update had it (in tenths of a percent).
			if (const std::uint64_t total = entry.ticksNow != 0 ? entry.ticksNow : entry.ticksTotal; index == 0 && total > 0)
				slot.progress = static_cast<std::uint32_t>(std::clamp<std::int64_t>(
					static_cast<std::int64_t>(entry.frames) * 1000 / static_cast<std::int64_t>(total), 0, 1000));
			state.queue.push_back(std::move(slot));
		}
	return state;
}
}
