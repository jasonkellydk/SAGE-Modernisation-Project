export module games.generalszh.presentation.camera.algorithms.selection_focus;
import std;

// MOVE_CAMERA_TO_SELECTION: ScriptActions::doModCameraMoveToSelection sums the positions of the drawables the player has
// selected (in the drawable list's order) and, with any, hands their average to W3DView::cameraModFinalMoveTo; none:
// nothing.
export namespace generalszh::presentation
{
inline std::optional<std::array<float, 3>> SelectionCentre(std::span<const std::array<float, 3>> positions)
{
	if (positions.empty())
		return std::nullopt;
	std::array<float, 3> sum{};
	for (const std::array<float, 3> &at : positions)
	{
		sum[0] += at[0];
		sum[1] += at[1];
		sum[2] += at[2];
	}
	const float count = static_cast<float>(static_cast<std::int32_t>(positions.size()));
	// destination.z /= count first, as the original does.
	sum[2] /= count;
	sum[0] /= count;
	sum[1] /= count;
	return sum;
}
}
