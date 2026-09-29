export module games.generalszh.hosts.game.game_client;
export import games.generalszh.shell.score.score_screen_view_model;
export import engine.core.serialization.byte_stream;
export import engine.audio.mixing.mixer;
import std;

import engine.level.model.level;
import games.generalszh.content.loading.content_loader;
export import games.generalszh.content.global.player_templates;
export import games.generalszh.session.session_view;
import engine.filesystem.core.virtual_file_system;
export import engine.effects.particles.simulation.particle_world;
import games.generalszh.presentation.rendering.object_rendering;
import games.generalszh.presentation.rendering.particle_rendering;
export import games.generalszh.presentation.effects.light_pulses;
export import games.generalszh.presentation.effects.scorch_marks;
export import games.generalszh.presentation.objects.components.track_marks;
export import games.generalszh.presentation.objects.algorithms.object_icon_layout;
import Graphics.Scene.Views.CameraState;

// The client side of a running map (any map: the shell map is just one its
// scripts play by themselves): the game session ticking at the fixed logic
// rate, the tactical camera (driven by scripts or the player) on real
// elapsed time, and the objects to draw, interpolated between the last two
// ticks. The interface stays light; session, content and camera live in the
// implementation.
export namespace generalszh::host
{
// A skirmish player's start for Start(): its map player and team, faction (PlayerTemplate store index)
// and start spot (0-based).
struct PlayerStart
{
	std::string player;
	std::string team;
	int playerTemplate{0};
	int startPosition{0};
};

using presentation::ObjectAnimationMode;
using presentation::ObjectInstance;
using presentation::ObjectModel;

// What the map's scripts set for the player's presentation: music, levels
// (percent), speech, and display switches. Audio and the in-game UI read it.
struct ClientSettings
{
	std::string musicTrack;
	std::int64_t musicVolume{100};
	std::int64_t soundVolume{100};
	std::int64_t speechVolume{100};
	std::vector<std::string> speechQueue;
	std::vector<std::string> disabledSounds;
	std::vector<std::pair<std::string, std::int64_t>> soundVolumeOverrides;
	bool evaEnabled{true};
	bool radarForced{false};
	// VictoryConditions' m_localPlayerDefeated: the local player's defeat was seen (the radar forced on then, once).
	bool localDefeatSeen{false};
	bool radarHidden{false}; // RADAR_DISABLE: the local player's radar is hidden (Radar::hide)
	bool borderShroudDisabled{false};
	bool drawIconUi{true};
	bool occlusion{true};
	bool particleCap{true};
	bool specialPowerDisplayDisabled{false};
	// The end of the game as the local player's scripts declared it (VICTORY, DEFEAT, LOCALDEFEAT: which window shows),
	// and whether that took the player's input away (doDisableInput: victory and defeat).
	enum class MatchEnd : std::uint8_t { None, Victory, Defeat, LocalDefeat, QuickVictory };
	MatchEnd matchEnd{MatchEnd::None};
	std::uint64_t matchEndTick{0};
	bool inputDisabled{false};
	// CAMERA_LETTERBOX_BEGIN / END (doLetterBoxMode): the bars on (the control bar hidden) or off, and how many times it
	// changed (the host times their fade on real time).
	bool letterbox{false};
	std::uint32_t letterboxChanges{0};
	// CAMERA_BW_MODE_BEGIN / END: the view in black and white, faded over `blackWhiteFrames`.
	bool blackWhite{false};
	std::int64_t blackWhiteFrames{0};
	// MOVIE_PLAY_FULLSCREEN / MOVIE_PLAY_RADAR: movies the scripts asked for, not yet played (the host takes them).
	std::vector<std::string> movies;
};

// The player's pointer this frame (pixels; button bits: 1 left, 2 right).
struct PointerState
{
	float x{0}, y{0};
	std::uint8_t down{0}, pressed{0}, released{0};
	bool shift{false}, ctrl{false}, alt{false};
	std::uint32_t timeMs{0};
	std::uint8_t arrows{0}; // arrow keys held: 1 up, 2 down, 4 left, 8 right
	float wheel{0};         // wheel notches turned since the last frame
	bool overInterface{false}; // over the in-game interface (the control bar): the world takes no clicks
};

// What the interaction draws over the world: the selection box being dragged, and each selected object's
// health bar (pixels: its top-left corner and width; health as a share).
struct SelectedMarker
{
	float x{0}, y{0}, width{0};
	float health{1};
	bool damaged{false}, reallyDamaged{false}, disabled{false};
};

// A floating text as it shows this frame (InGameUI::drawFloatingText): centred on x, its top at y, colour and alpha.
struct OverlayText
{
	std::u16string text;
	float x{0}, y{0};
	std::array<float, 4> color{1, 1, 1, 1};
};

// A world animation's image this frame (InGameUI::updateAndDrawWorldAnimations): its mapped image, centred on
// its screen point, its own size times `scale` (1.3 over the camera zoom), at `alpha`.
struct OverlayImage
{
	std::string image;
	float x{0}, y{0};
	float scale{1};
	float alpha{1};
	// An object's icon instead (Drawable::drawIconUI): placed by object_icon_layout once its image's size is known,
	// against its health region (`region`) or, for veterancy, its health box position (x, y) and width over the zoom.
	enum class Placement : std::uint8_t
	{
		Centred,
		Icon,
		Veterancy,
	};
	Placement placement{Placement::Centred};
	presentation::ObjectIcon icon{presentation::ObjectIcon::Disabled};
	presentation::IconRegion region;
	float iconScale{1};
	float healthBoxWidth{0};
	float zoom{1};
};

// The messages at the top of the screen (InGameUI::postDraw): oldest first, each line under the last from `messageAt`
// (pixels), in its colour with a black drop at its alpha.
struct OverlayMessage
{
	std::u16string text;
	std::array<float, 4> color{1, 1, 1, 1};
};

// A superweapon countdown line (InGameUI's SuperweaponInfo as drawn): none shown is a skipped one (still being built);
// its power's name (GUI:<name> labels it), m:ss, its colour this frame, and whether it is ready (the bold font).
struct OverlaySuperweapon
{
	bool shown{false};
	std::string power;
	std::u16string time;
	std::array<float, 4> color{1, 1, 1, 1};
	bool ready{false};
};

// The military caption (InGameUI's military subtitle as drawn): its lines typed so far, its colour this frame, whether
// the block after the last letter shows, and where it starts (on an 800x600 screen).
struct OverlayCaption
{
	bool shown{false};
	std::vector<std::u16string> lines;
	std::array<float, 4> color{1, 1, 1, 1};
	bool block{false};
	std::array<float, 2> at{10, 380};
};

struct InGameOverlay
{
	bool boxActive{false};
	std::vector<OverlayMessage> messages;
	std::array<float, 2> messageAt{10, 10};
	std::array<float, 4> box{};
	std::vector<SelectedMarker> selected;
	std::vector<OverlayText> texts;
	std::vector<OverlayImage> images;
	// The superweapon countdowns (none while a script hides them), from their position (a share of the screen).
	std::vector<OverlaySuperweapon> superweapons;
	std::array<float, 2> superweaponAt{0.9f, 0.01f};
	OverlayCaption caption;
	// The named timers (InGameUI's, as drawn): from `namedTimerAt` (a share of the screen) up, a line each; right-aligned
	// there from the middle rightwards; the ready font for a countdown at 0:00; its colour.
	struct NamedTimerLine
	{
		std::u16string text;
		bool ready{false};
		std::array<float, 4> color{1, 1, 1, 1};
	};
	std::vector<NamedTimerLine> namedTimers;
	std::array<float, 2> namedTimerAt{0.7f, 0.7f};
	// The screen fade (ScriptEngine's m_fade as W3DStatusCircle draws it): none, add, subtract, saturate, multiply; value.
	std::uint8_t fade{0};
	float fadeValue{0.0f};
};

class GameClient
{
public:
	using TerrainHeight = std::function<float(float x, float y)>;

	GameClient();
	~GameClient();
	GameClient(const GameClient &) = delete;
	GameClient &operator=(const GameClient &) = delete;

	// Binds the game content, starts the session on the level and puts the
	// camera at the level's InitialCameraPosition waypoint (else the centre).
	// `files` is the mounted install (for sounds).
	void Load(content::ContentLoader &loader, const engine::filesystem::VirtualFileSystem &files, const engine::level::Level &level,
		TerrainHeight terrainHeight, std::array<float, 2> playable,
		float aspectRatio, std::uint64_t seed);
	// A new match on `level` in place of the one playing (the content stays): its session (a skirmish's
	// seats and starting players), camera and presentation start afresh; what played stops.
	// `checkpoint`: a saved game's state to go on from (its match made on the same level and options); false (and the
	// match not started) when it does not fit.
	bool Start(const engine::level::Level &level, TerrainHeight terrainHeight, std::array<float, 2> playable, float aspectRatio,
		std::uint64_t seed, std::vector<std::string> seats = {}, std::vector<PlayerStart> starts = {},
		std::string cameraMarker = "InitialCameraPosition", std::optional<std::uint8_t> soloDifficulty = std::nullopt, bool challenge = false,
		std::span<const std::byte> checkpoint = {}, std::optional<session::NetworkMatchOptions> network = std::nullopt, std::int32_t rankPoints = 0);
	// The local player's skill points now (Player::getSkillPoints: what the campaign carries on); 0 with none.
	std::int32_t LocalSkillPoints() const;
	// The match's state now (a saved game's); empty with no match.
	std::vector<std::byte> Checkpoint() const;
	// Every player's score keeping as the score screen shows it: its name (as SetPlayerNames gave it, else its map name),
	// colour, base side and whether it is the local player's ally or enemy.
	std::vector<shell::ScorePlayer> ScoreBoard() const;
	// What a saved game keeps of this side of the game (the original's TacticalView, InGameUI, Radar and audio xfer):
	// the camera, and what the scripts set for the local player (music, volumes, sounds off, radar, EVA, the special
	// power display, input). Read back after Start from the same save.
	void SaveClientState(engine::core::serialization::ByteWriter &writer) const;
	bool LoadClientState(engine::core::serialization::ByteReader &reader);
	// Diagnostics: the matches started from now on write every script action they run to stderr.
	void TraceScripts(bool on);

	// Game speed: 1 is the original's 30 ticks per second. The host runs
	// TicksPerSecond() ticks per real second; the camera's scripted moves
	// are timed in game time and so follow the speed as well.
	void SetGameSpeed(float speed) noexcept;
	// Keeps the camera looking at a map point over whatever the scripts do (captures, checks).
	void PinCamera(float x, float y) noexcept;
	// Where the view's corners meet the plane at `z` (upper left, upper right, lower right, lower left).
	std::optional<std::array<std::array<float, 2>, 4>> ViewCornersAtZ(float z) const;
	// The player looks at a map point (a radar click).
	void UserLookAt(float x, float y);
	float GameSpeed() const noexcept;
	double TicksPerSecond() const noexcept;
	// The mouse cursor the match asks for (InGameUI::setMouseCursor): its Mouse::MouseCursor (content::MouseCursorKind)
	// and direction; none with no match.
	std::optional<std::array<std::uint8_t, 2>> MouseCursor() const;
	// Starts waiting for the target of `button` (a control bar command button of `source`: InGameUI::setGUICommand).
	// `shortcutType`: a general's powers shortcut's power type (its source then found anew each frame).
	void BeginTargeting(ecs::Entity source, std::string_view button, std::string_view shortcutType = {});

	// One logic tick: the session, then the camera commands its scripts queued.
	void Tick();
	// Once per rendered frame with the frame's elapsed real time; `alpha` is
	// how far (0..1) the frame is past the last tick.
	void Update(float deltaSeconds, float alpha);
	// In a match: the pointer as it is this frame (the interaction reads it on the next Update), the viewport,
	// and what to draw for it.
	void Point(const PointerState &pointer) noexcept;
	void SetViewport(float width, float height) noexcept;
	InGameOverlay Overlay() const;
	// GUI:AddCash, the pattern a delivery's floating text follows.
	void SetAddCashText(std::u16string pattern);
	void SetLoseCashText(std::u16string pattern);
	// The game's strings by label (GameText::fetch), for the messages the match shows.
	void SetLabels(std::function<std::u16string(std::string_view)> labels);

	void ApplyView(Graphics::CameraState &camera) const;
	std::array<float, 3> Eye() const;
	std::span<const ObjectInstance> Objects() const noexcept;
	// The model a look (numbered in Objects()) draws, and the animation it plays.
	ObjectModel ModelFor(std::uint32_t look) const;
	// Learns the animation clips of looks not known yet (their frames and rate) as the renderer loads them.
	void KnowClips(const std::function<std::optional<std::pair<float, float>>(std::uint32_t look)> &clipOf);

	const ClientSettings &Settings() const noexcept;
	// The movies the scripts asked for since the last call (MOVIE_PLAY_FULLSCREEN / MOVIE_PLAY_RADAR), in order.
	std::vector<std::string> TakeMovies();

	std::string UnportedSummary() const;
	// PlayerTemplate.ini in store order (a game setup names factions by index).
	const content::PlayerTemplates &PlayerTemplates() const noexcept;
	// The sound output in use ("XAudio2", "SDL3" or "none").
	std::string_view AudioOutputName() const noexcept;
	// Plays a front-end sound (a button's click), without a position.
	void PlayInterfaceSound(std::string_view event);
	// A front-end voice in place of the last one (empty: the last one stops).
	void PlayInterfaceVoice(std::string_view event);
	// A shell interaction the map's scripts may wait on (signalUIInteract), onto the command bus.
	void SignalUiInteraction(std::string_view hook);
	// A menu's music (the credits'), faded in over what plays; RestoreMusic brings back what played.
	void PlayMenuMusic(std::string_view track);
	void RestoreMusic();
	// The player's volumes (Options.ini, percent): music, 2D and 3D sounds, speech.
	void SetUserVolumes(int music, int sound2D, int sound3D, int speech);
	// The options' detail level (StaticGameLOD name): how long tracks last.
	void SetDetailLevel(std::string_view level);
	// The Retaliation option (GlobalData::m_clientRetaliationModeEnabled): the local player's simulation is told of it
	// once a second while they differ (Player::update's MSG_ENABLE_RETALIATION_MODE).
	void SetRetaliation(bool enabled) noexcept;
	// The tracks on the terrain, each's texture and edges oldest first, with the limits they draw by.
	std::vector<presentation::TrackView> Tracks() const;
	// The sound settings' defaults (AudioSettings.ini, percent), for the options' defaults.
	std::array<int, 5> DefaultVolumes() const;
	std::string AudioSummary() const;
	// Particles alive and the systems objects carry (condition states, damage states).
	std::string EffectsSummary() const;
	// The effects' particles, and how far (0..1) the frame is past their last step.
	const engine::effects::ParticleWorld *Particles() const noexcept;
	// This frame's laser beams.
	std::vector<presentation::BeamSegment> Lasers() const;
	// This frame's dynamic lights (light pulses, police light bars).
	std::vector<presentation::ShownLight> Lights() const;
	// The scorch marks on the terrain (none before the world is up).
	const presentation::ScorchMarks *Scorches() const;
	float ParticleAlpha() const noexcept;
	std::size_t EntityCount() const;
	std::size_t WorkerCount() const;
	// For the in-game interface: the running game's view (none before one runs), what the local player has
	// selected, the local player (none: an observer), and an order of theirs onto the command bus.
	session::SessionView *View() noexcept;
	std::vector<ecs::Entity> Selection() const;
	// The audio mixer (movies play their sound on it).
	engine::audio::Mixer &AudioMixer() noexcept;
	// A new selection of these (GUI_COMMAND_SELECT_ALL_UNITS_OF_TYPE: deselectAllDrawables, then each selected).
	void SelectOnly(const std::vector<ecs::Entity> &entities);
	std::optional<std::uint32_t> LocalPlayer() const;
	void Submit(const commands::GameCommand &command);
	// Starts placing `structure` for `builder` (the control bar's DOZER_CONSTRUCT: InGameUI::placeBuildAvailable).
	void BeginPlacement(ecs::Entity builder, std::string_view structure);
	// The local player's own match scripts (the original's MultiplayerScripts.scb, added in a game of more than one
	// team): they run after each tick on this machine only, answering for the local player (their victory, defeat,
	// side), and play and show what they declare. Never part of the lockstep game. Each list runs as its own player's
	// (the local human's skirmish side scripts, its music, besides MultiplayerScripts.scb's).
	// The names the match's messages give its players (a human's own, an AI's GUI:EasyAI and so on), by player
	// name, and the text for one defeated (GUI:PlayerHasBeenDefeated).
	void SetPlayerNames(std::vector<std::pair<std::string, std::u16string>> names, std::u16string defeatedText);
	void UseMatchScripts(std::vector<engine::level::ScriptList> lists);

private:
	struct State;
	std::unique_ptr<State> m_state;
};
}
