export module games.generalszh.session.session_view;
import std;
export import games.generalszh.gameplay.orders.algorithms.command_availability;
export import games.generalszh.gameplay.orders.algorithms.build_tooltip_facts;

export import engine.level.model.level;
export import engine.jobs.job_system;
export import engine.ecs.core.world;
export import games.generalszh.content.loading.game_content;
export import engine.gameplay.common.spatial.systems.snapshot_system;
export import engine.gameplay.rts.combat.resources.shots;
export import engine.gameplay.rts.combat.systems.point_defense_system;
export import engine.gameplay.rts.death.resources.death_events;
export import engine.gameplay.rts.topple.resources.topple_events;
export import games.generalszh.scripting.camera_vocabulary;
export import games.generalszh.scripting.presentation_vocabulary;
export import games.generalszh.commands.game_commands;
export import engine.net.lockstep.command_recording;

// What the presentation sees of a running game, and how it runs one: a
// narrow, read-mostly view (the tick's snapshot, events and the content
// behind them, the scripts' camera and client commands) and a simulation
// behind the in-process lockstep relay. The client depends on this only,
// never on the session and its systems.
export namespace generalszh::session
{
// A skirmish or LAN player's start (placeNetworkBuildingsForPlayer): its map player and team, the
// faction (PlayerTemplate store index) and the start spot (0-based: Player_<n+1>_Start).
struct StartingPlayer
{
	std::string player;
	std::string team;
	int playerTemplate{0};
	int startPosition{0};
};

// GameLogic::startNewGame's scenery: shrubbery placed only with trees on (UseTrees), and clearing fluff made a client
// prop (forceFluffToProp: below High detail, Custom never; a network game uses trees and forces fluff).
struct ScenerySetup
{
	bool useTrees{true};
	bool forceFluffToProp{false};
};

// The scenery for the detail level (StaticGameLODLevel: 0 Low .. 3 VeryHigh, 4 Custom) and the player's trees option.
inline ScenerySetup ScenerySetupFor(std::int32_t detailLevel, bool useTrees, bool networkGame) noexcept
{
	if (networkGame)
		return {true, true};
	return {useTrees, detailLevel < 2};
}

struct SessionOptions
{
	std::uint64_t seed{0};
	// Scheduler workers when the session makes its own job system (see
	// `jobs`); 0 = one per hardware thread but one.
	std::size_t workers{0};
	// CAMERA_MOVEMENT_FINISHED is answered by the presentation's camera.
	std::function<bool()> cameraMovementFinished;
	// The map player each lockstep seat plays (empty or unknown: observer,
	// whose commands are ignored). The shell map has none.
	std::vector<std::string> seats;
	// This machine's seat (its local player's): 0 but in a LAN game.
	std::uint32_t localSeat{0};
	// The process's shared job system (one pool for simulation, presentation
	// and effects). Null: the session makes its own, as headless tools and tests do.
	engine::jobs::JobSystem *jobs{nullptr};
	// Presentation's side-table components, registered with the world's own
	// before it is finalized (they never touch the state hash or checkpoints).
	std::function<void(ecs::World &)> presentationComponents;
	// Skirmish and LAN games: each player's start, placed after the map's own objects.
	std::vector<StartingPlayer> starts;
	// A campaign or challenge mission (GAME_SINGLE_PLAYER) and its difficulty (0 easy, 1 normal, 2 hard): the
	// single-player difficulty bonuses.
	bool singlePlayer{false};
	std::uint8_t difficulty{1};
	// GameLogic::m_rankPointsToAddAtGameStart (the campaign's rank points, CampaignManager::getRankPoints): a new
	// single-player game gives each human player these skill points once it is set up.
	std::int32_t rankPointsAtStart{0};
	// A Generals' Challenge mission: the local player (seat 0) takes "ThePlayer"'s relationships, and scripts naming
	// "ThePlayer" mean it.
	bool challenge{false};
	// Diagnostics: every script action run is written to stderr ("script <tick> <script>: <action>").
	bool scriptTrace{false};
	// How many ticks a sound event plays (HAS_FINISHED_SPEECH / AUDIO): from the install's sound files, the same on
	// every machine; none: no time at all.
	std::function<std::uint64_t(std::string_view)> soundLengthTicks;
	// GameEngine::isTimeFrozen's camera part: a camera move that freezes time (CAMERA_MOD_FREEZE_TIME) still going. Only
	// for a game on one machine (the original ignores it in network games).
	std::function<bool()> cameraFreezesTime;
	// GameEngine::isTimeFrozen: never in a network game (TheNetwork), a script's freeze included.
	bool timeFreezes{true};
	// NAMED_SELECTED: whether the local player has the entity selected (the presentation's selection; none: never, as in
	// a multiplayer game).
	std::function<bool(ecs::Entity)> selected;
	// RecorderClass: the game's ticks are recorded from its start for a replay (skirmish and LAN games).
	bool record{false};
	// GameLogic::startNewGame's scenery (see ScenerySetup).
	ScenerySetup scenery;
};

// A player's score keeping as the score screen shows it (the original's Player and its ScoreKeeper): its name and
// PlayerTemplate, whether it is human, listed on the score screen (setListInScoreScreen) and of a playable side, how
// the local player (seat 0's) regards it (0 neutral, 1 allies, 2 enemies; itself: allies), and its tallies.
struct PlayerScore
{
	std::string name;
	std::string playerTemplate;
	bool human{false};
	bool listed{true};
	bool playable{false};
	bool local{false};
	std::uint8_t relation{0};
	std::int64_t unitsBuilt{0}, unitsLost{0}, unitsDestroyed{0};
	std::int64_t buildingsBuilt{0}, buildingsLost{0}, buildingsDestroyed{0};
	std::int64_t moneyEarned{0}, moneySpent{0};
	std::int64_t score{0}; // calculateScore
};

// A rider put into or taken out of a container this tick (OpenContain::onContaining / onRemoving): each one's
// definition and where it was (a container that died this tick: where it died); `has*` false when not known.
struct CargoMove
{
	std::uint32_t containerDefinition{0};
	std::uint32_t riderDefinition{0};
	Engine::Math::FixedVector3 containerAt;
	Engine::Math::FixedVector3 riderAt;
	bool hasContainer{false};
	bool hasRider{false};
	bool entered{false};
};

class SessionView
{
public:
	virtual ~SessionView() = default;

	virtual const content::GameContent &Content() const noexcept = 0;
	virtual const content::ObjectDefinition &Definition(std::uint32_t index) const = 0;
	// Whether a definition has an AI module (Object::getAIUpdateInterface).
	virtual bool DefinitionHasAi(std::uint32_t index) const noexcept = 0;
	// Player::isLogicalRetaliationModeEnabled.
	virtual bool RetaliationEnabled(std::uint32_t player) const noexcept = 0;
	virtual std::optional<std::uint32_t> DefinitionOf(ecs::Entity entity) const = 0;
	// The entity a script name refers to (invalid when none).
	virtual ecs::Entity Named(std::string_view name) const = 0;
	// The members of the team a script names (getTeamNamed), newest first as the original's member list; none: empty.
	virtual std::vector<ecs::Entity> TeamMembers(std::string_view team) = 0;
	// Flying (Object::isUsingAirborneLocomotor and isAboveTerrainOrWater): a following camera turns with it.
	virtual bool FlyingAboveSurface(ecs::Entity entity) const = 0;
	// TerrainLogic::getExtent's upper corner (the active boundary): the camera keeps within it.
	virtual Engine::Math::FixedVector2 PlayableExtent() const = 0;
	// GameEngine::isTimeFrozen: the last tick ran only its scripts (a script's freeze, a camera move's).
	virtual bool TimeFrozen() const noexcept = 0;
	// A script counter's or countdown timer's value (ScriptEngine::getCounter: 0 for one never set).
	virtual std::int64_t ScriptCounter(std::string_view name) const = 0;
	virtual const content::WeaponContent *WeaponContentOf(std::uint32_t weapon) const = 0;
	// How many weapons are in play (weapon indices run below it).
	virtual std::uint32_t WeaponCount() const = 0;
	// Armors in play (Health::armor indices run below the count) and each one's name (none: plain).
	virtual std::uint32_t ArmorCount() const = 0;
	virtual std::string_view ArmorName(std::uint32_t armor) const = 0;
	// An object definition by name (none: unknown).
	virtual const content::ObjectDefinition *ObjectNamed(std::string_view name) const = 0;
	// The tick's point defense laser shots.
	virtual const engine::gameplay::PointDefenseShots &DefenseShots() const noexcept = 0;
	virtual std::string_view ModelName(std::uint32_t id) const = 0;
	virtual std::string_view DeathEffectName(engine::gameplay::DeathEffectKind kind, std::uint32_t id) const = 0;

	// This tick's.
	virtual const engine::gameplay::VisibleObjects &Visible() const noexcept = 0;
	virtual const engine::gameplay::FiredShots &Fired() const noexcept = 0;
	virtual const std::vector<engine::gameplay::Impact> &Impacts() const noexcept = 0;
	virtual std::vector<engine::gameplay::DeathEvent> DeathEvents() const = 0;
	virtual std::vector<engine::gameplay::ToppleEvent> ToppleEvents() const = 0;
	virtual std::vector<CargoMove> CargoMoves() const = 0;
	virtual std::vector<scripting::CameraScriptCommand> &CameraCommands() noexcept = 0;
	virtual std::vector<scripting::ClientScriptCommand> &ClientCommands() noexcept = 0;

	// The simulation's world, for presentation systems: they read its
	// components between ticks and write only their side tables.
	virtual ecs::World &World() noexcept = 0;
	virtual const ecs::World &World() const noexcept = 0;
	// How many definitions the session has taken on (indices below it are valid).
	virtual std::size_t DefinitionCount() const noexcept = 0;
	// The map player a lockstep seat plays (none: an observer).
	virtual std::optional<std::uint32_t> SeatPlayer(std::uint32_t seat) const = 0;

	virtual std::string UnportedSummary() const = 0;
	virtual std::size_t EntityCount() const noexcept = 0;
	// The control bar's facts (ControlBar::populateCommand / getCommandAvailability): the command set an object shows
	// (an upgrade's override included; empty: none), each of its buttons' state, and a player's side (its
	// PlayerTemplate's Side: the control bar scheme's).
	virtual std::string CommandSetOf(ecs::Entity entity) const = 0;
	virtual gameplay::ButtonState CommandAvailability(ecs::Entity entity, const content::CommandButtonContent &button) = 0;
	// What the build tooltip asks the game of a command button for a player and its first selected object
	// (ControlBar::populateBuildTooltipLayout).
	virtual gameplay::BuildTooltipFacts BuildTooltip(std::uint32_t player, ecs::Entity selected, const content::CommandButtonContent &button) = 0;
	// AIUpdateInterface::isQuickPathAvailable (or a CLIFF locomotor over a cliff cell) for the move hint; false without an AI.
	virtual bool QuickPathAvailable(ecs::Entity unit, Engine::Math::FixedVector2 to) = 0;
	virtual std::string PlayerSide(std::uint32_t player) const = 0;
	// Every player's score keeping, by player index (the score screen's).
	virtual std::vector<PlayerScore> Scores() const = 0;
	// This machine's seat (SessionOptions::localSeat).
	virtual std::uint32_t LocalSeat() const noexcept = 0;
	// A player's objects of a type or a scripts' object type list (evaluatePlayerUnitCondition; ignoreDead: the living).
	virtual std::int64_t CountPlayerObjects(std::uint32_t player, const std::string &types, bool ignoreDead) const = 0;
	// Its PlayerTemplate (FactionAmerica...); empty: none.
	virtual std::string PlayerTemplateName(std::uint32_t player) const = 0;
	// The simulation's tick (TheGameLogic->getFrame()).
	virtual std::uint64_t CurrentTick() const noexcept = 0;
	// Whether `builder` may put up `structure` at `at` facing `facing` now (BuildAssistant::canMakeUnit and
	// isLocationLegalToBuild), and a definition's index by name (none: unknown). `specialPowerConstruct`: placed for the
	// builder's SPECIAL_POWER_CONSTRUCT button (canMakeUnit's special case).
	virtual bool CanBuildAt(ecs::Entity builder, std::string_view structure, Engine::Math::FixedVector2 at, Engine::Math::TurnAngle facing,
		bool specialPowerConstruct) = 0;
	virtual std::optional<std::uint32_t> DefinitionIndex(std::string_view name) = 0;
	// Whether a player's order may fire `power` of `source` at `target` now (canDoSpecialPowerAtObject with its source's
	// module fully ready).
	virtual bool CanTargetWithPower(ecs::Entity source, std::string_view power, ecs::Entity target) = 0;
	// ActionManager::canEnterObject(COMBATDROP_INTO) for a player's order: whether `transport` may combat drop into `target`.
	virtual bool CanCombatDropInto(ecs::Entity transport, ecs::Entity target) = 0;
	// A charging power button's inverse clock, per mille (1000: none).
	virtual std::uint32_t CommandClock(ecs::Entity entity, const content::CommandButtonContent &button) = 0;
	// The general's powers shortcut bar (Player::findMostReadyShortcutSpecialPowerOfType, hasAnyShortcutSpecialPower,
	// countReadyShortcutSpecialPowersOfType, findAnyExistingObjectWithThingTemplate,
	// findMostReadyShortcutSpecialPowerForThing; `type` a special power type, `object` an object type).
	virtual std::optional<ecs::Entity> ShortcutPowerSource(std::uint32_t player, std::string_view type) = 0;
	virtual bool HasAnyShortcutPower(std::uint32_t player) = 0;
	virtual std::int32_t ReadyShortcutPowers(std::uint32_t player, std::string_view type) = 0;
	virtual std::optional<ecs::Entity> AnyObjectOfType(std::uint32_t player, const std::string &object) = 0;
	virtual std::vector<ecs::Entity> ObjectsOfType(std::uint32_t player, const std::string &object) = 0;
	// CommandXlat's viewCommandCenter (VIEW_COMMAND_CENTER) and iNeedAHero (SELECT_HERO) among the player's objects.
	virtual std::optional<ecs::Entity> CommandCenterToView(std::uint32_t player) = 0;
	virtual std::optional<ecs::Entity> HeroToSelect(std::uint32_t player) const = 0;
	virtual std::optional<ecs::Entity> MostReadyPowerOfType(std::uint32_t player, const std::string &object) = 0;
	virtual std::size_t WorkerCount() const noexcept = 0;
};

// A game run through the in-process lockstep relay (single player is a
// match of one).
class ClientMatch
{
public:
	virtual ~ClientMatch() = default;
	// Runs the next tick if the relay has it; false when it has not.
	virtual bool Advance() = 0;
	// Changes when the game is restored from a checkpoint (nothing to interpolate from).
	virtual std::uint64_t Generation() const noexcept = 0;
	virtual SessionView &View() noexcept = 0;
	// A command from this machine onto the bus (it applies on the tick the relay gives it).
	virtual void Submit(const commands::GameCommand &command) = 0;
	// The whole simulation state between ticks (a saved game's), for ResumeClientMatch.
	virtual std::vector<std::byte> Checkpoint() const = 0;
	// The ticks recorded so far (SessionOptions::record); none when not recording.
	virtual const engine::net::CommandRecording *Recording() const noexcept { return nullptr; }
	// A replay's playback: done once every recorded tick has run (RecorderClass::stopPlayback), and the first tick after
	// which its state hash differed from the recording's (handleCRCMessage's mismatch). None: not a replay.
	struct Playback
	{
		bool done{false};
		std::optional<std::uint64_t> mismatch;
		std::uint64_t endTick{0};
	};
	virtual std::optional<Playback> PlaybackState() const noexcept { return std::nullopt; }
};

std::unique_ptr<ClientMatch> MakeClientMatch(const engine::level::Level &level, const content::GameContent &content, SessionOptions options);
// A LAN game (the lockstep relay over reliable UDP): this machine's seat and every seat's count, and the host's
// address; none: this machine hosts, its relay taking connections on `port`. Its ticks run once every seat is taken.
struct NetworkMatchOptions
{
	std::uint32_t seat{0};
	std::uint32_t players{1};
	std::optional<std::uint32_t> hostAddress;
	std::uint16_t port{8088};
	std::uint32_t inputDelay{2};
};
std::unique_ptr<ClientMatch> MakeNetworkMatch(const engine::level::Level &level, const content::GameContent &content, SessionOptions options,
	NetworkMatchOptions network);

// A game resumed from a checkpoint (a saved game) on the same level, content and options; null when it does not fit.
std::unique_ptr<ClientMatch> ResumeClientMatch(const engine::level::Level &level, const content::GameContent &content, SessionOptions options,
	std::span<const std::byte> checkpoint);

// A replay played back (RecorderClass::playbackFile / updatePlayback): the game started as it was, each recorded tick's
// commands run on their tick (this machine's own orders are not: cullBadCommands), its state hashes checked.
std::unique_ptr<ClientMatch> MakeReplayMatch(const engine::level::Level &level, const content::GameContent &content, SessionOptions options,
	engine::net::CommandRecording recording);
}
