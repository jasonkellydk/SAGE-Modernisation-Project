export module games.generalszh.commands.game_commands;
import std;

export import engine.net.lockstep.protocol;
export import engine.ecs.core.entity;
export import Engine.Core.Math.FixedVector;
import engine.events.schema.message_registry;

// Everything a player can order, as the commands that travel the game's
// command bus (through the lockstep server, also in single player). Each
// has a stable name, and encodes deterministically; entities are named by
// their ECS handles, which every peer's deterministic simulation agrees on.
export namespace generalszh::commands
{
using Engine::Math::Fixed;
using Engine::Math::FixedVector2;

struct MoveTo
{
	std::vector<ecs::Entity> units;
	FixedVector2 destination;
	// The spot's height as clicked (MSG_DO_MOVETO's Coord3D: W3DView::screenToTerrain, a bridge deck over the terrain
	// taken when nearer the eye): the layer the goal is on follows from it (getLayerForDestination).
	Fixed height{};
};

struct Attack
{
	std::vector<ecs::Entity> units;
	ecs::Entity target;
};

struct Stop
{
	std::vector<ecs::Entity> units;
};

// Force-fire on a spot of ground (MSG_DO_FORCE_ATTACK_GROUND: groupAttackPosition, no shot limit).
struct AttackPosition
{
	std::vector<ecs::Entity> units;
	FixedVector2 position;
};

// Dock with a warehouse or supply centre (AIUpdateInterface::aiDock from the player: it becomes the trucks' own).
struct Dock
{
	std::vector<ecs::Entity> units;
	ecs::Entity dock;
};

// Drive a special power's effect to a spot while it lasts (MSG_DO_SPECIAL_POWER_OVERRIDE_DESTINATION:
// setSpecialPowerOverridableDestination: the Particle Cannon's beam).
struct SpecialPowerDestination
{
	std::vector<ecs::Entity> units;
	FixedVector2 position; // on the ground there
};

// Go and be repaired at a repair pad (MSG_GET_REPAIRED: groupGetRepaired -> aiGetRepaired).
struct GetRepaired
{
	std::vector<ecs::Entity> units;
	ecs::Entity depot;
};

// Go into something (MSG_ENTER: groupEnter -> aiEnter): board a transport, take over an unmanned vehicle, make a car
// bomb of a vehicle.
struct Enter
{
	std::vector<ecs::Entity> units;
	ecs::Entity target;
};

// Go on building a structure left unfinished (MSG_RESUME_CONSTRUCTION: groupResumeConstruction -> aiResumeConstruction).
struct ResumeConstruction
{
	std::vector<ecs::Entity> units;
	ecs::Entity target;
};

// Repair a structure (MSG_DO_REPAIR: groupRepair -> aiRepair).
struct Repair
{
	std::vector<ecs::Entity> units;
	ecs::Entity target;
};

// Go into a heal pad to be healed (MSG_GET_HEALED: groupGetHealed -> aiGetHealed).
struct GetHealed
{
	std::vector<ecs::Entity> units;
	ecs::Entity target;
};

// A player's guard order (MSG_DO_GUARD_POSITION / MSG_DO_GUARD_OBJECT from the control bar's GUARD buttons): the units
// guard a spot or an object, in a GuardMode (0 normal, 1 without pursuit, 2 flying units only).
struct GuardPosition
{
	std::vector<ecs::Entity> units;
	FixedVector2 position;
	std::uint8_t mode{0};
};

// A player's FIRE_WEAPON button (MSG_DO_WEAPON / _AT_LOCATION / _AT_OBJECT): the units' weapon in `slot`, locked for the
// attack (LOCKED_TEMPORARILY), fired at most `maxShots` times (0: no limit) at where each stands (`at` 0), at `position`
// (1) or at `target` (2).
struct FireWeapon
{
	std::vector<ecs::Entity> units;
	std::uint8_t slot{0};
	std::uint8_t at{0};
	std::uint32_t maxShots{0};
	FixedVector2 position;
	ecs::Entity target;
};

// A player's attack-move (MSG_DO_ATTACKMOVETO): the units move to the spot, taking on what they come across.
struct AttackMoveTo
{
	std::vector<ecs::Entity> units;
	FixedVector2 position;
	Fixed height{}; // as MoveTo's
};

// A player's EVACUATE (MSG_EVACUATE): the units let their riders out.
struct Evacuate
{
	std::vector<ecs::Entity> units;
};

// A player's rally point for a factory of theirs (MSG_SET_RALLY_POINT).
struct SetRallyPoint
{
	ecs::Entity factory;
	FixedVector2 position;
};

struct GuardObject
{
	std::vector<ecs::Entity> units;
	ecs::Entity target;
	std::uint8_t mode{0};
};

struct UseSpecialPower
{
	ecs::Entity source;
	std::string power;
	FixedVector2 target;
	// At the spot (MSG_DO_SPECIAL_POWER_AT_LOCATION), else with no target (MSG_DO_SPECIAL_POWER: a button needing none).
	bool atLocation{true};
	// The button's command options (the message's options argument: OPTION_ONE .. THREE pick a Strategy Center's plan).
	std::uint32_t options{0};
	// The message's angle (in turn units): the placed structure's facing of a SPECIAL_POWER_CONSTRUCT (PlaceEventTranslator);
	// every other order sends INVALID_ANGLE, which the creation lists take as 0.
	std::uint32_t angle{0};
};

// A player buys a science with its general's points (the original's MSG_PURCHASE_SCIENCE, from the General's Powers
// screen): by its Science.ini name.
struct PurchaseScience
{
	std::string science;
};

// A special power fired at an object (the original's MSG_DO_SPECIAL_POWER_AT_OBJECT: a cash hack at a supply centre).
struct UseSpecialPowerAtObject
{
	ecs::Entity source;
	std::string power;
	ecs::Entity target;
};

// A player touched the shell's user interface (the original's signalUIInteract: "ShellMainMenuSkirmishHighlighted"...):
// the scripts' FLAG conditions of that name hold for the tick.
struct SignalUi
{
	std::string hook;
};

// Research an upgrade at a building (ProductionUpdate::queueUpgrade), or stop researching it (cancelUpgrade).
struct ResearchUpgrade
{
	ecs::Entity building;
	std::string upgrade;
};

struct CancelResearch
{
	ecs::Entity building;
	std::string upgrade;
};

// Queue a unit at a factory (MSG_QUEUE_UNIT_CREATE: the control bar's UNIT_BUILD), or cancel one by its production id
// (MSG_CANCEL_UNIT_CREATE).
struct QueueUnit
{
	ecs::Entity factory;
	std::string unit;
};

struct CancelUnit
{
	ecs::Entity factory;
	std::uint32_t productionId{0};
};

// Switch the overcharge of each unit that has one (MSG_TOGGLE_OVERCHARGE: AIGroup::groupToggleOvercharge).
struct ToggleOvercharge
{
	std::vector<ecs::Entity> units;
};

// Put each unit on its MINE_CLEARING_DETAIL weapon set (MSG_SET_MINE_CLEARING_DETAIL: AIGroup::setMineClearingDetail(TRUE),
// sent as a USES_MINE_CLEARING_WEAPONSET button is pressed).
struct SetMineClearingDetail
{
	std::vector<ecs::Entity> units;
};

// Lock each unit to a weapon slot (MSG_SWITCH_WEAPONS: AIGroup::setWeaponLockForGroup LOCKED_PERMANENTLY).
struct SwitchWeapon
{
	std::vector<ecs::Entity> units;
	std::uint32_t slot{0};
};

// Sell a structure (BuildAssistant::sellObject).
struct Sell
{
	ecs::Entity building;
};

// Cancel a structure still under construction (MSG_DOZER_CANCEL_CONSTRUCT: GameLogic::onDozerCancelConstruct).
struct CancelConstruction
{
	ecs::Entity building;
};

// Have a dozer or worker build a structure (DozerAIUpdate::construct): what, where, facing which way (turn units).
struct BuildStructure
{
	ecs::Entity builder;
	std::string structure;
	FixedVector2 position;
	std::uint32_t facing{0};
};

// Send a dozer or worker to a structure: to go on building it, or to repair it (a builder's right-click on it).
struct WorkOn
{
	ecs::Entity builder;
	ecs::Entity structure;
};

// A debug cheat (the original's MSG_META_DEMO_KILL_ALL_ENEMIES, debug builds only): everything of every player who
// counts the sender an enemy is killed. On the command bus, so every peer kills the same.
struct KillAllEnemies
{
};

// The quit menu's Surrender (MSG_SELF_DESTRUCT): the sender's player gives up, its assets going to a living ally when
// `transferToAlly` (GameLogic::onSelfDestruct).
// The sender's Retaliation option (MSG_ENABLE_RETALIATION_MODE): its player's things strike back at their aggressors or not.
struct EnableRetaliation
{
	bool enabled{true};
};

struct SelfDestruct
{
	bool transferToAlly{true};
};

// MSG_COMBATDROP_AT_OBJECT (target set: dropping into it) or MSG_COMBATDROP_AT_LOCATION (at `position`).
struct CombatDrop
{
	std::vector<ecs::Entity> units;
	ecs::Entity target;
	FixedVector2 position;
};

// A player's beacon at a spot (MSG_PLACE_BEACON).
struct PlaceBeacon
{
	FixedVector2 position;
};

// A player's MSG_REMOVE_BEACON over its selection: its own beacons go, another's it hides from itself.
struct RemoveBeacon
{
	std::vector<ecs::Entity> units;
};

// A player's MSG_SET_BEACON_TEXT over its selection (UTF-8; empty: no caption).
struct SetBeaconText
{
	std::vector<ecs::Entity> units;
	std::string text;
};

// A player's in-game chat line (ConnectionManager::sendChat / NETCOMMANDTYPE_CHAT): its text (UTF-8) and the game slots
// it is for (bit n: slot n). It changes nothing in the simulation; each machine shows it to its own player.
struct Chat
{
	std::string text;
	std::uint32_t slots{0};
};

// The local presentation's music (AudioManager): the track playing and how often it has played through since it was
// set, reported whenever that changes so a single-player mission's MUSIC_TRACK_HAS_COMPLETED can ask it.
struct MusicProgress
{
	std::string track;
	std::uint32_t completions{0};
};

// A player's MSG_EXECUTE_RAILED_TRANSPORT (the EXECUTE_RAILED_TRANSPORT button): each railed transport of the selection
// sets off along its next path (AIGroup::groupExecuteRailedTransport).
struct ExecuteRailedTransport
{
	std::vector<ecs::Entity> units;
};

// A player's MSG_EXIT (a rider's button in the selected container's inventory: GUI_COMMAND_EXIT_CONTAINER): that rider,
// one of the sender's, gets out of the selected container (GameLogic::onExit: aiExit(container, CMD_FROM_PLAYER)).
struct Exit
{
	ecs::Entity rider;
	ecs::Entity container;
};

// A player's MSG_CREATE_TEAMn (SelectionTranslator's MSG_META_CREATE_TEAMn: the selected objects it controls): hotkey squad
// `squad` (0..9) becomes those objects (Player::processCreateTeamGameMessage).
struct CreateTeam
{
	std::int32_t squad{0};
	std::vector<ecs::Entity> units;
};

// A player's MSG_SELECT_TEAMn (SelectionTranslator::onMetaSelectTeam when it performs the selection): the logic's copy of
// its selection becomes the squad's live members, and a squad with any counts for its academy's control groups
// (Player::processSelectTeamGameMessage). The local selection itself is the presentation's.
struct SelectTeam
{
	std::int32_t squad{0};
};

// A player's MSG_DO_CHEER (CommandTranslator's MSG_META_ALL_CHEER in a network game): every selected object of theirs
// cheers (GameLogic::onDoCheer -> AIGroup::groupCheer).
struct Cheer
{
	std::vector<ecs::Entity> units;
};

// A player's MSG_ADD_WAYPOINT (a move click in waypoint mode, Alt held): the selection, as one group, adds the spot to
// its path (GameLogic::onAddWaypoint -> AIGroup::groupMoveToPosition with addWaypoint).
struct AddWaypoint
{
	std::vector<ecs::Entity> units;
	FixedVector2 destination;
	Fixed height{}; // as MoveTo's
};

// A player's MSG_INTERNET_HACK (the control bar's HACK_INTERNET button): each selected hacker starts hacking the
// internet (GameLogic::onInternetHack -> AIGroup::groupHackInternet).
struct HackInternet
{
	std::vector<ecs::Entity> units;
};

// A player's MSG_CREATE_FORMATION (CommandTranslator's MSG_META_CREATE_FORMATION, CommandMap CREATE_FORMATION: Ctrl+F):
// the selected group makes a formation of itself, or breaks the one it is (GameLogicDispatch ->
// AIGroup::groupCreateFormation).
struct CreateFormation
{
	std::vector<ecs::Entity> units;
};

// A player's MSG_DO_SCATTER (CommandTranslator's MSG_META_SCATTER, CommandMap SCATTER: X): the selected group scatters
// from its centre (GameLogicDispatch -> AIGroup::groupScatter).
struct Scatter
{
	std::vector<ecs::Entity> units;
};

using GameCommand = std::variant<MoveTo, Attack, Stop, Dock, UseSpecialPower, SignalUi, ResearchUpgrade, CancelResearch, Sell, BuildStructure, WorkOn,
	QueueUnit, CancelUnit, KillAllEnemies, UseSpecialPowerAtObject, PurchaseScience, ToggleOvercharge, SwitchWeapon, SelfDestruct, AttackPosition, GetRepaired, SpecialPowerDestination, Enter, EnableRetaliation, GuardPosition, GuardObject, SetRallyPoint, Evacuate, AttackMoveTo, FireWeapon, PlaceBeacon, RemoveBeacon, SetBeaconText, CombatDrop, MusicProgress, ResumeConstruction, Repair, GetHealed, ExecuteRailedTransport, Exit, CancelConstruction, CreateTeam, Cheer, CreateFormation,
	AddWaypoint, HackInternet, Scatter, SelectTeam, SetMineClearingDetail, Chat>;
}

export namespace engine::events
{
template<>
struct MessageTraits<generalszh::commands::MoveTo>
{
	static constexpr std::string_view StableName = "generalszh.command.move_to";
	static constexpr std::uint32_t Version = 2; // 2: its height
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::Attack>
{
	static constexpr std::string_view StableName = "generalszh.command.attack";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::AttackPosition>
{
	static constexpr std::string_view StableName = "generalszh.command.attack_position";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::Stop>
{
	static constexpr std::string_view StableName = "generalszh.command.stop";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::Dock>
{
	static constexpr std::string_view StableName = "generalszh.command.dock";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::SpecialPowerDestination>
{
	static constexpr std::string_view StableName = "generalszh.command.special_power_destination";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::GetRepaired>
{
	static constexpr std::string_view StableName = "generalszh.command.get_repaired";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::Enter>
{
	static constexpr std::string_view StableName = "generalszh.command.enter";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::ResumeConstruction>
{
	static constexpr std::string_view StableName = "generalszh.command.resume_construction";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::Repair>
{
	static constexpr std::string_view StableName = "generalszh.command.repair";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::GetHealed>
{
	static constexpr std::string_view StableName = "generalszh.command.get_healed";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::SignalUi>
{
	static constexpr std::string_view StableName = "generalszh.command.signal_ui";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::ResearchUpgrade>
{
	static constexpr std::string_view StableName = "generalszh.command.research_upgrade";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::CancelResearch>
{
	static constexpr std::string_view StableName = "generalszh.command.cancel_research";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::QueueUnit>
{
	static constexpr std::string_view StableName = "generalszh.command.queue_unit";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::CancelUnit>
{
	static constexpr std::string_view StableName = "generalszh.command.cancel_unit";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::KillAllEnemies>
{
	static constexpr std::string_view StableName = "generalszh.command.kill_all_enemies";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::EnableRetaliation>
{
	static constexpr std::string_view StableName = "generalszh.command.enable_retaliation";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::CombatDrop>
{
	static constexpr std::string_view StableName = "generalszh.command.combat_drop";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::PlaceBeacon>
{
	static constexpr std::string_view StableName = "generalszh.command.place_beacon";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::CreateTeam>
{
	static constexpr std::string_view StableName = "generalszh.command.create_team";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::SelectTeam>
{
	static constexpr std::string_view StableName = "generalszh.command.select_team";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::Cheer>
{
	static constexpr std::string_view StableName = "generalszh.command.cheer";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::AddWaypoint>
{
	static constexpr std::string_view StableName = "generalszh.command.add_waypoint";
	static constexpr std::uint32_t Version = 2; // 2: its height
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::HackInternet>
{
	static constexpr std::string_view StableName = "generalszh.command.hack_internet";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::CreateFormation>
{
	static constexpr std::string_view StableName = "generalszh.command.create_formation";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::Scatter>
{
	static constexpr std::string_view StableName = "generalszh.command.scatter";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::RemoveBeacon>
{
	static constexpr std::string_view StableName = "generalszh.command.remove_beacon";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::ExecuteRailedTransport>
{
	static constexpr std::string_view StableName = "generalszh.command.execute_railed_transport";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::Exit>
{
	static constexpr std::string_view StableName = "generalszh.command.exit";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::MusicProgress>
{
	static constexpr std::string_view StableName = "generalszh.command.music_progress";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::Chat>
{
	static constexpr std::string_view StableName = "generalszh.command.chat";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Transient;
};

template<>
struct MessageTraits<generalszh::commands::SetBeaconText>
{
	static constexpr std::string_view StableName = "generalszh.command.set_beacon_text";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::SelfDestruct>
{
	static constexpr std::string_view StableName = "generalszh.command.self_destruct";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::SetMineClearingDetail>
{
	static constexpr std::string_view StableName = "generalszh.command.set_mine_clearing_detail";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::ToggleOvercharge>
{
	static constexpr std::string_view StableName = "generalszh.command.toggle_overcharge";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::SwitchWeapon>
{
	static constexpr std::string_view StableName = "generalszh.command.switch_weapon";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::Sell>
{
	static constexpr std::string_view StableName = "generalszh.command.sell";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::CancelConstruction>
{
	static constexpr std::string_view StableName = "generalszh.command.cancel_construction";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::BuildStructure>
{
	static constexpr std::string_view StableName = "generalszh.command.build_structure";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::WorkOn>
{
	static constexpr std::string_view StableName = "generalszh.command.work_on";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::UseSpecialPower>
{
	static constexpr std::string_view StableName = "generalszh.command.use_special_power";
	static constexpr std::uint32_t Version = 4;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::GuardPosition>
{
	static constexpr std::string_view StableName = "generalszh.command.guard_position";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::FireWeapon>
{
	static constexpr std::string_view StableName = "generalszh.command.fire_weapon";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::AttackMoveTo>
{
	static constexpr std::string_view StableName = "generalszh.command.attack_move_to";
	static constexpr std::uint32_t Version = 2; // 2: its height
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::Evacuate>
{
	static constexpr std::string_view StableName = "generalszh.command.evacuate";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::SetRallyPoint>
{
	static constexpr std::string_view StableName = "generalszh.command.set_rally_point";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::GuardObject>
{
	static constexpr std::string_view StableName = "generalszh.command.guard_object";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::PurchaseScience>
{
	static constexpr std::string_view StableName = "generalszh.command.purchase_science";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};

template<>
struct MessageTraits<generalszh::commands::UseSpecialPowerAtObject>
{
	static constexpr std::string_view StableName = "generalszh.command.use_special_power_at_object";
	static constexpr std::uint32_t Version = 1;
	static constexpr MessageKind Kind = MessageKind::Command;
	static constexpr RecordPolicy Recording = RecordPolicy::Recordable;
};
}

export namespace generalszh::commands
{
template<typename T>
constexpr std::uint64_t TypeOf() noexcept
{
	return engine::events::HashMessageKey(engine::events::MessageTraits<T>::StableName);
}

// Whether a command of stable key `type` goes into a replay (RecordPolicy::Recordable). RecorderClass only writes the
// GameMessages the logic ran; chat travels as NetChatCommandMsg (NetCommandList, never a GameMessage), so a replay has
// none. Unknown keys are kept: playback rejects them itself.
inline bool Recorded(std::uint64_t type) noexcept
{
	return [type]<std::size_t... I>(std::index_sequence<I...>) {
		bool recorded = true;
		((TypeOf<std::variant_alternative_t<I, GameCommand>>() == type
				? (recorded = engine::events::MessageTraits<std::variant_alternative_t<I, GameCommand>>::Recording == engine::events::RecordPolicy::Recordable, 0)
				: 0),
			...);
		return recorded;
	}(std::make_index_sequence<std::variant_size_v<GameCommand>>{});
}

namespace detail
{
using engine::core::serialization::ByteReader;
using engine::core::serialization::ByteWriter;

inline void Put(ByteWriter &writer, ecs::Entity entity)
{
	writer.U32(entity.index);
	writer.U32(entity.generation);
}

inline void Put(ByteWriter &writer, FixedVector2 point)
{
	writer.I64(point.x.Raw());
	writer.I64(point.y.Raw());
}

inline void Put(ByteWriter &writer, const std::vector<ecs::Entity> &units)
{
	writer.U32(static_cast<std::uint32_t>(units.size()));
	for (const ecs::Entity unit : units)
		Put(writer, unit);
}

inline std::optional<ecs::Entity> GetEntity(ByteReader &reader)
{
	const auto index = reader.U32();
	const auto generation = reader.U32();
	if (!index || !generation)
		return std::nullopt;
	ecs::Entity entity;
	entity.index = *index;
	entity.generation = *generation;
	return entity;
}

inline std::optional<FixedVector2> GetPoint(ByteReader &reader)
{
	const auto x = reader.I64();
	const auto y = reader.I64();
	if (!x || !y)
		return std::nullopt;
	return FixedVector2{Fixed::FromRaw(*x), Fixed::FromRaw(*y)};
}

inline std::optional<std::vector<ecs::Entity>> GetUnits(ByteReader &reader)
{
	const auto count = reader.U32();
	if (!count || *count > 4096)
		return std::nullopt;
	std::vector<ecs::Entity> units;
	for (std::uint32_t index = 0; index < *count; ++index)
	{
		const auto unit = GetEntity(reader);
		if (!unit)
			return std::nullopt;
		units.push_back(*unit);
	}
	return units;
}
}

inline engine::net::CommandEnvelope Encode(const GameCommand &command)
{
	engine::core::serialization::ByteWriter writer;
	std::uint64_t type = 0;
	std::visit(
		[&](const auto &value) {
			using T = std::decay_t<decltype(value)>;
			type = TypeOf<T>();
			if constexpr (std::is_same_v<T, MoveTo> || std::is_same_v<T, AddWaypoint>)
			{
				detail::Put(writer, value.units);
				detail::Put(writer, value.destination);
				writer.I64(value.height.Raw());
			}
			else if constexpr (std::is_same_v<T, AttackPosition>)
			{
				detail::Put(writer, value.units);
				detail::Put(writer, value.position);
			}
			else if constexpr (std::is_same_v<T, Attack>)
			{
				detail::Put(writer, value.units);
				detail::Put(writer, value.target);
			}
			else if constexpr (std::is_same_v<T, Dock>)
			{
				detail::Put(writer, value.units);
				detail::Put(writer, value.dock);
			}
			else if constexpr (std::is_same_v<T, GetRepaired>)
			{
				detail::Put(writer, value.units);
				detail::Put(writer, value.depot);
			}
			else if constexpr (std::is_same_v<T, Enter> || std::is_same_v<T, ResumeConstruction> || std::is_same_v<T, Repair> || std::is_same_v<T, GetHealed>)
			{
				detail::Put(writer, value.units);
				detail::Put(writer, value.target);
			}
			else if constexpr (std::is_same_v<T, SpecialPowerDestination>)
			{
				detail::Put(writer, value.units);
				detail::Put(writer, value.position);
			}
			else if constexpr (std::is_same_v<T, Stop> || std::is_same_v<T, ToggleOvercharge> || std::is_same_v<T, Cheer> || std::is_same_v<T, CreateFormation> ||
				std::is_same_v<T, HackInternet> || std::is_same_v<T, Scatter> || std::is_same_v<T, SetMineClearingDetail>)
				detail::Put(writer, value.units);
			else if constexpr (std::is_same_v<T, SwitchWeapon>)
			{
				detail::Put(writer, value.units);
				writer.U32(value.slot);
			}
			else if constexpr (std::is_same_v<T, SignalUi>)
				writer.Text(value.hook);
			else if constexpr (std::is_same_v<T, ResearchUpgrade> || std::is_same_v<T, CancelResearch>)
			{
				detail::Put(writer, value.building);
				writer.Text(value.upgrade);
			}
			else if constexpr (std::is_same_v<T, Sell> || std::is_same_v<T, CancelConstruction>)
				detail::Put(writer, value.building);
			else if constexpr (std::is_same_v<T, QueueUnit>)
			{
				detail::Put(writer, value.factory);
				writer.Text(value.unit);
			}
			else if constexpr (std::is_same_v<T, CancelUnit>)
			{
				detail::Put(writer, value.factory);
				writer.U32(value.productionId);
			}
			else if constexpr (std::is_same_v<T, WorkOn>)
			{
				detail::Put(writer, value.builder);
				detail::Put(writer, value.structure);
			}
			else if constexpr (std::is_same_v<T, BuildStructure>)
			{
				detail::Put(writer, value.builder);
				writer.Text(value.structure);
				detail::Put(writer, value.position);
				writer.U32(value.facing);
			}
			else if constexpr (std::is_same_v<T, KillAllEnemies>)
				return; // nothing but its sender
			else if constexpr (std::is_same_v<T, SelfDestruct>)
				writer.Flag(value.transferToAlly);
			else if constexpr (std::is_same_v<T, PlaceBeacon>)
				detail::Put(writer, value.position);
			else if constexpr (std::is_same_v<T, Chat>)
			{
				writer.Text(value.text);
				writer.U32(value.slots);
			}
			else if constexpr (std::is_same_v<T, CombatDrop>)
			{
				detail::Put(writer, value.units);
				detail::Put(writer, value.target);
				detail::Put(writer, value.position);
			}
			else if constexpr (std::is_same_v<T, RemoveBeacon>)
				detail::Put(writer, value.units);
			else if constexpr (std::is_same_v<T, CreateTeam>)
			{
				writer.U32(static_cast<std::uint32_t>(value.squad));
				detail::Put(writer, value.units);
			}
			else if constexpr (std::is_same_v<T, SelectTeam>)
				writer.U32(static_cast<std::uint32_t>(value.squad));
			else if constexpr (std::is_same_v<T, SetBeaconText>)
			{
				detail::Put(writer, value.units);
				writer.Text(value.text);
			}
			else if constexpr (std::is_same_v<T, ExecuteRailedTransport>)
				detail::Put(writer, value.units);
			else if constexpr (std::is_same_v<T, Exit>)
			{
				detail::Put(writer, value.rider);
				detail::Put(writer, value.container);
			}
			else if constexpr (std::is_same_v<T, MusicProgress>)
			{
				writer.Text(value.track);
				writer.U32(value.completions);
			}
			else if constexpr (std::is_same_v<T, EnableRetaliation>)
				writer.Flag(value.enabled);
			else if constexpr (std::is_same_v<T, PurchaseScience>)
				writer.Text(value.science);
			else if constexpr (std::is_same_v<T, GuardPosition>)
			{
				detail::Put(writer, value.units);
				detail::Put(writer, value.position);
				writer.U32(value.mode);
			}
			else if constexpr (std::is_same_v<T, Evacuate>)
				detail::Put(writer, value.units);
			else if constexpr (std::is_same_v<T, FireWeapon>)
			{
				detail::Put(writer, value.units);
				writer.U32(value.slot);
				writer.U32(value.at);
				writer.U32(value.maxShots);
				detail::Put(writer, value.position);
				detail::Put(writer, value.target);
			}
			else if constexpr (std::is_same_v<T, AttackMoveTo>)
			{
				detail::Put(writer, value.units);
				detail::Put(writer, value.position);
				writer.I64(value.height.Raw());
			}
			else if constexpr (std::is_same_v<T, SetRallyPoint>)
			{
				detail::Put(writer, value.factory);
				detail::Put(writer, value.position);
			}
			else if constexpr (std::is_same_v<T, GuardObject>)
			{
				detail::Put(writer, value.units);
				detail::Put(writer, value.target);
				writer.U32(value.mode);
			}
			else if constexpr (std::is_same_v<T, UseSpecialPowerAtObject>)
			{
				detail::Put(writer, value.source);
				writer.Text(value.power);
				detail::Put(writer, value.target);
			}
			else
			{
				detail::Put(writer, value.source);
				writer.Text(value.power);
				detail::Put(writer, value.target);
				writer.Flag(value.atLocation);
				writer.U32(value.options);
				writer.U32(value.angle);
			}
		},
		command);
	return {type, 0, writer.Take()};
}

// Null for an unknown type or a malformed payload (a peer's bad message is dropped, never trusted).
inline std::optional<GameCommand> Decode(const engine::net::CommandEnvelope &envelope)
{
	engine::core::serialization::ByteReader reader(envelope.payload);
	std::optional<GameCommand> command;
	if (envelope.type == TypeOf<MoveTo>())
	{
		auto units = detail::GetUnits(reader);
		const auto destination = detail::GetPoint(reader);
		const auto height = reader.I64();
		if (units && destination && height)
			command = MoveTo{std::move(*units), *destination, Fixed::FromRaw(*height)};
	}
	else if (envelope.type == TypeOf<AttackPosition>())
	{
		auto units = detail::GetUnits(reader);
		const auto position = detail::GetPoint(reader);
		if (units && position)
			command = AttackPosition{std::move(*units), *position};
	}
	else if (envelope.type == TypeOf<Attack>())
	{
		auto units = detail::GetUnits(reader);
		const auto target = detail::GetEntity(reader);
		if (units && target)
			command = Attack{std::move(*units), *target};
	}
	else if (envelope.type == TypeOf<Dock>())
	{
		auto units = detail::GetUnits(reader);
		const auto dock = detail::GetEntity(reader);
		if (units && dock)
			command = Dock{std::move(*units), *dock};
	}
	else if (envelope.type == TypeOf<SpecialPowerDestination>())
	{
		auto units = detail::GetUnits(reader);
		const auto position = detail::GetPoint(reader);
		if (units && position)
			command = SpecialPowerDestination{std::move(*units), *position};
	}
	else if (envelope.type == TypeOf<GetRepaired>())
	{
		auto units = detail::GetUnits(reader);
		const auto depot = detail::GetEntity(reader);
		if (units && depot)
			command = GetRepaired{std::move(*units), *depot};
	}
	else if (envelope.type == TypeOf<Enter>())
	{
		auto units = detail::GetUnits(reader);
		const auto target = detail::GetEntity(reader);
		if (units && target)
			command = Enter{std::move(*units), *target};
	}
	else if (envelope.type == TypeOf<ResumeConstruction>() || envelope.type == TypeOf<Repair>() || envelope.type == TypeOf<GetHealed>())
	{
		auto units = detail::GetUnits(reader);
		const auto target = detail::GetEntity(reader);
		if (units && target)
		{
			if (envelope.type == TypeOf<ResumeConstruction>())
				command = ResumeConstruction{std::move(*units), *target};
			else if (envelope.type == TypeOf<Repair>())
				command = Repair{std::move(*units), *target};
			else
				command = GetHealed{std::move(*units), *target};
		}
	}
	else if (envelope.type == TypeOf<Stop>())
	{
		if (auto units = detail::GetUnits(reader))
			command = Stop{std::move(*units)};
	}
	else if (envelope.type == TypeOf<Cheer>())
	{
		if (auto units = detail::GetUnits(reader))
			command = Cheer{std::move(*units)};
	}
	else if (envelope.type == TypeOf<HackInternet>())
	{
		if (auto units = detail::GetUnits(reader))
			command = HackInternet{std::move(*units)};
	}
	else if (envelope.type == TypeOf<AddWaypoint>())
	{
		auto units = detail::GetUnits(reader);
		const auto destination = detail::GetPoint(reader);
		const auto height = reader.I64();
		if (units && destination && height)
			command = AddWaypoint{std::move(*units), *destination, Fixed::FromRaw(*height)};
	}
	else if (envelope.type == TypeOf<CreateFormation>())
	{
		if (auto units = detail::GetUnits(reader))
			command = CreateFormation{std::move(*units)};
	}
	else if (envelope.type == TypeOf<Scatter>())
	{
		if (auto units = detail::GetUnits(reader))
			command = Scatter{std::move(*units)};
	}
	else if (envelope.type == TypeOf<SwitchWeapon>())
	{
		auto units = detail::GetUnits(reader);
		const auto slot = reader.U32();
		if (units && slot && *slot < 3)
			command = SwitchWeapon{std::move(*units), *slot};
	}
	else if (envelope.type == TypeOf<ToggleOvercharge>())
	{
		if (auto units = detail::GetUnits(reader))
			command = ToggleOvercharge{std::move(*units)};
	}
	else if (envelope.type == TypeOf<SetMineClearingDetail>())
	{
		if (auto units = detail::GetUnits(reader))
			command = SetMineClearingDetail{std::move(*units)};
	}
	else if (envelope.type == TypeOf<UseSpecialPower>())
	{
		const auto source = detail::GetEntity(reader);
		auto power = reader.Text();
		const auto target = detail::GetPoint(reader);
		const auto atLocation = reader.Flag();
		const auto options = reader.U32();
		const auto angle = reader.U32();
		if (source && power && target && atLocation && options && angle)
			command = UseSpecialPower{*source, std::move(*power), *target, *atLocation, *options, *angle};
	}
	else if (envelope.type == TypeOf<GuardPosition>())
	{
		auto units = detail::GetUnits(reader);
		const auto position = detail::GetPoint(reader);
		const auto mode = reader.U32();
		if (units && position && mode && *mode <= 2)
			command = GuardPosition{std::move(*units), *position, static_cast<std::uint8_t>(*mode)};
	}
	else if (envelope.type == TypeOf<FireWeapon>())
	{
		auto units = detail::GetUnits(reader);
		const auto slot = reader.U32();
		const auto at = reader.U32();
		const auto shots = reader.U32();
		const auto position = detail::GetPoint(reader);
		const auto target = detail::GetEntity(reader);
		if (units && slot && at && shots && position && target && *slot < 3 && *at <= 2)
			command = FireWeapon{std::move(*units), static_cast<std::uint8_t>(*slot), static_cast<std::uint8_t>(*at), *shots, *position, *target};
	}
	else if (envelope.type == TypeOf<AttackMoveTo>())
	{
		auto units = detail::GetUnits(reader);
		const auto position = detail::GetPoint(reader);
		const auto height = reader.I64();
		if (units && position && height)
			command = AttackMoveTo{std::move(*units), *position, Fixed::FromRaw(*height)};
	}
	else if (envelope.type == TypeOf<Evacuate>())
	{
		if (auto units = detail::GetUnits(reader))
			command = Evacuate{std::move(*units)};
	}
	else if (envelope.type == TypeOf<SetRallyPoint>())
	{
		const auto factory = detail::GetEntity(reader);
		const auto position = detail::GetPoint(reader);
		if (factory && position)
			command = SetRallyPoint{*factory, *position};
	}
	else if (envelope.type == TypeOf<GuardObject>())
	{
		auto units = detail::GetUnits(reader);
		const auto target = detail::GetEntity(reader);
		const auto mode = reader.U32();
		if (units && target && mode && *mode <= 2)
			command = GuardObject{std::move(*units), *target, static_cast<std::uint8_t>(*mode)};
	}
	else if (envelope.type == TypeOf<PurchaseScience>())
	{
		auto science = reader.Text();
		if (science && science->size() <= 128)
			command = PurchaseScience{std::move(*science)};
	}
	else if (envelope.type == TypeOf<UseSpecialPowerAtObject>())
	{
		const auto source = detail::GetEntity(reader);
		auto power = reader.Text();
		const auto target = detail::GetEntity(reader);
		if (source && power && power->size() <= 128 && target)
			command = UseSpecialPowerAtObject{*source, std::move(*power), *target};
	}
	else if (envelope.type == TypeOf<ResearchUpgrade>() || envelope.type == TypeOf<CancelResearch>())
	{
		const auto building = detail::GetEntity(reader);
		auto upgrade = reader.Text();
		if (building && upgrade && upgrade->size() <= 128)
		{
			if (envelope.type == TypeOf<ResearchUpgrade>())
				command = ResearchUpgrade{*building, std::move(*upgrade)};
			else
				command = CancelResearch{*building, std::move(*upgrade)};
		}
	}
	else if (envelope.type == TypeOf<WorkOn>())
	{
		const auto builder = detail::GetEntity(reader);
		const auto structure = detail::GetEntity(reader);
		if (builder && structure)
			command = WorkOn{*builder, *structure};
	}
	else if (envelope.type == TypeOf<BuildStructure>())
	{
		const auto builder = detail::GetEntity(reader);
		auto structure = reader.Text();
		const auto position = detail::GetPoint(reader);
		const auto facing = reader.U32();
		if (builder && structure && structure->size() <= 128 && position && facing)
			command = BuildStructure{*builder, std::move(*structure), *position, *facing};
	}
	else if (envelope.type == TypeOf<KillAllEnemies>())
		command = KillAllEnemies{};
	else if (envelope.type == TypeOf<SelfDestruct>())
	{
		if (const auto transfer = reader.Flag())
			command = SelfDestruct{*transfer};
	}
	else if (envelope.type == TypeOf<CombatDrop>())
	{
		auto units = detail::GetUnits(reader);
		const auto target = detail::GetEntity(reader);
		const auto position = detail::GetPoint(reader);
		if (units && target && position)
			command = CombatDrop{std::move(*units), *target, *position};
	}
	else if (envelope.type == TypeOf<PlaceBeacon>())
	{
		if (const auto position = detail::GetPoint(reader))
			command = PlaceBeacon{*position};
	}
	else if (envelope.type == TypeOf<Chat>())
	{
		auto text = reader.Text();
		const auto slots = reader.U32();
		if (text && slots)
			command = Chat{std::move(*text), *slots};
	}
	else if (envelope.type == TypeOf<CreateTeam>())
	{
		const auto squad = reader.U32();
		auto units = detail::GetUnits(reader);
		if (squad && units)
			command = CreateTeam{static_cast<std::int32_t>(*squad), std::move(*units)};
	}
	else if (envelope.type == TypeOf<SelectTeam>())
	{
		if (const auto squad = reader.U32())
			command = SelectTeam{static_cast<std::int32_t>(*squad)};
	}
	else if (envelope.type == TypeOf<RemoveBeacon>())
	{
		if (auto units = detail::GetUnits(reader))
			command = RemoveBeacon{std::move(*units)};
	}
	else if (envelope.type == TypeOf<SetBeaconText>())
	{
		auto units = detail::GetUnits(reader);
		auto text = reader.Text();
		if (units && text && text->size() <= 1024)
			command = SetBeaconText{std::move(*units), std::move(*text)};
	}
	else if (envelope.type == TypeOf<ExecuteRailedTransport>())
	{
		if (auto units = detail::GetUnits(reader))
			command = ExecuteRailedTransport{std::move(*units)};
	}
	else if (envelope.type == TypeOf<Exit>())
	{
		const auto rider = detail::GetEntity(reader);
		const auto container = detail::GetEntity(reader);
		if (rider && container)
			command = Exit{*rider, *container};
	}
	else if (envelope.type == TypeOf<MusicProgress>())
	{
		auto track = reader.Text();
		const auto completions = reader.U32();
		if (track && completions && track->size() <= 256)
			command = MusicProgress{std::move(*track), *completions};
	}
	else if (envelope.type == TypeOf<EnableRetaliation>())
	{
		if (const auto enabled = reader.Flag())
			command = EnableRetaliation{*enabled};
	}
	else if (envelope.type == TypeOf<Sell>())
	{
		if (const auto building = detail::GetEntity(reader))
			command = Sell{*building};
	}
	else if (envelope.type == TypeOf<CancelConstruction>())
	{
		if (const auto building = detail::GetEntity(reader))
			command = CancelConstruction{*building};
	}
	else if (envelope.type == TypeOf<QueueUnit>())
	{
		const auto factory = detail::GetEntity(reader);
		auto unit = reader.Text();
		if (factory && unit && unit->size() <= 128)
			command = QueueUnit{*factory, std::move(*unit)};
	}
	else if (envelope.type == TypeOf<CancelUnit>())
	{
		const auto factory = detail::GetEntity(reader);
		const auto id = reader.U32();
		if (factory && id)
			command = CancelUnit{*factory, *id};
	}
	else if (envelope.type == TypeOf<SignalUi>())
	{
		if (auto hook = reader.Text(); hook && hook->size() <= 64)
			command = SignalUi{std::move(*hook)};
	}
	if (!command || !reader.AtEnd())
		return std::nullopt;
	return command;
}
}
