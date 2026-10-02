export module games.generalszh.presentation.hud.algorithms.experience_bar;
import std;

// Where the control bar's general's experience bar draws (W3DCommandBarGenExpDraw, the GeneralsExp window): nothing at
// no progress into the rank (and never the window's own look); else a bar rising `progress` percent (at most 100) of
// the window's height from its bottom: the bottom cap (GenExpBarBottom1) at the bottom, the centre piece (GenExpBar1)
// repeated down from under the top cap to the bottom cap, its last piece clipped at the bottom cap, and the top cap
// (GenExpBarTop1) at the bar's top, drawn upside down (the original passes its rectangle bottom first). When the bar
// is too short for any centre the caps sit stacked at the bottom, the top one the right way up. Each piece spans the
// window's width; pixels from the window's top-left, whole as the original's Int coordinates.
export namespace generalszh::presentation
{
enum class ExperiencePiece : std::uint8_t
{
	Bottom, // GenExpBarBottom1
	Centre, // GenExpBar1
	Top     // GenExpBarTop1
};

struct ExperienceQuad
{
	ExperiencePiece piece{ExperiencePiece::Centre};
	int top{0}, bottom{0}; // its rectangle down the window
	float vTop{0.0f}, vBottom{1.0f}; // the image's rows it shows there (vTop > vBottom: upside down)
};

inline std::vector<ExperienceQuad> LayOutExperienceBar(int progress, int height, int bottomHeight, int centreHeight, int topHeight)
{
	std::vector<ExperienceQuad> quads;
	if (progress <= 0)
		return quads;
	progress = std::min(progress, 100);
	const int range = height * progress / 100;
	const int bottomEnd = height - bottomHeight;
	const int topStart = height - range - topHeight;
	int centreSpan = bottomEnd - topStart;
	if (centreSpan <= 0)
	{
		quads.push_back({ExperiencePiece::Bottom, height - bottomHeight, height});
		const int top = height - bottomHeight - topHeight;
		quads.push_back({ExperiencePiece::Top, top, top + topHeight});
		return quads;
	}
	const int pieces = centreHeight > 0 ? centreSpan / centreHeight : 0;
	int y = topStart;
	for (int piece = 0; piece < pieces; ++piece, y += centreHeight)
		quads.push_back({ExperiencePiece::Centre, y, y + centreHeight});
	// The last piece, clipped where the bottom cap starts (setClipRegion).
	centreSpan = bottomEnd - y;
	if (centreSpan > 0 && centreHeight > 0)
		quads.push_back({ExperiencePiece::Centre, y, bottomEnd, 0.0f, static_cast<float>(centreSpan) / static_cast<float>(centreHeight)});
	quads.push_back({ExperiencePiece::Bottom, bottomEnd, height});
	// Its rectangle from (top: height - range) down to (bottom: height - range - topHeight): upside down.
	quads.push_back({ExperiencePiece::Top, height - range - topHeight, height - range, 1.0f, 0.0f});
	return quads;
}
}
