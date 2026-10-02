export module games.generalszh.presentation.hud.algorithms.replay_fast_forward;
import std;

// GlobalData m_TiVOFastMode (MetaEventTranslator's TOGGLE_FAST_FORWARD_REPLAY, F, only in a replay): GameEngine::execute
// stops limiting the frame rate, so the logic runs a frame each pass of the loop as fast as it can, and W3DDisplay::draw
// renders only on logic frames where frame % 30 == 1 (or while paused, or with the mode off). GameLogic::startNewGame
// turns it off for every new game; toggling it says GUI:FF_ON / GUI:FF_OFF.
export namespace generalszh::presentation
{
// Whether the game loop runs unlimited (a logic frame each pass).
constexpr bool ReplayFastForwardUnlimited(bool fastMode, bool replay) noexcept { return fastMode && replay; }

// Whether this pass renders.
constexpr bool ReplayFastForwardDraws(bool fastMode, bool replay, bool paused, std::uint64_t logicFrame) noexcept
{
	return logicFrame % 30 == 1 || !(!paused && fastMode && replay);
}

constexpr std::string_view ReplayFastForwardLabel(bool fastMode) noexcept { return fastMode ? "GUI:FF_ON" : "GUI:FF_OFF"; }
}
