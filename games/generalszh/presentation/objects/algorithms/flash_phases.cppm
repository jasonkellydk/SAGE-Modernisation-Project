export module games.generalszh.presentation.objects.algorithms.flash_phases;
import std;

// The accelerating flashes of a capture under way and of a defector's cover: a phase that grows each logic frame by a
// share of how far along it is, flashing each time it leaves an odd whole number (the original's
// ((Int)phase) & 1 turning from 1 to 0). Floats, as the original's.
export namespace generalszh::presentation
{
// SpecialAbilityUpdate::continuePreparation (DoCaptureFX): a third of 1 - left / PreparationTime (at least 1) a frame.
inline bool StepCaptureFlash(float &phase, std::uint32_t prepLeft, std::uint32_t preparationTicks) noexcept
{
	const bool last = (static_cast<std::int64_t>(phase) & 1) != 0;
	const float denominator = static_cast<float>(std::max<std::uint32_t>(1, preparationTicks));
	phase += (1.0f - static_cast<float>(prepLeft) / denominator) / 3.0f;
	return last && (static_cast<std::int64_t>(phase) & 1) == 0;
}

// ObjectDefectionHelper::update: half of 1 - left / DEFECTION_DETECTION_TIME_MAX (ten seconds of frames) a frame.
inline bool StepDefectorFlash(float &phase, std::uint64_t left) noexcept
{
	const bool last = (static_cast<std::int64_t>(phase) & 1) != 0;
	phase += 0.5f * (1.0f - static_cast<float>(left) / 300.0f);
	return last && (static_cast<std::int64_t>(phase) & 1) == 0;
}
}
