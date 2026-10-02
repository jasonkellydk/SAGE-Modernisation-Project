export module games.generalszh.content.images.animation_2d;
import std;

export import engine.config.document.document;
import engine.config.binding.values;

// Animation2D.ini: flip-book animations of mapped images (Anim2DTemplate):
// its images in order (Image lines, or ImageSequence: NumberImages of
// BASE000, BASE001 ...), how it plays (AnimationMode) and how many logic
// frames each image shows (AnimationDelay ms, whole frames rounded up, at
// least one), and whether it starts on a random image.
export namespace generalszh::content
{
enum class Anim2DMode : std::uint8_t
{
	None,
	Once,
	OnceBackwards,
	Loop,
	LoopBackwards,
	PingPong,
	PingPongBackwards,
};

struct Anim2DTemplate
{
	std::vector<std::string> images;
	Anim2DMode mode{Anim2DMode::None};
	std::uint32_t framesPerImage{1};
	bool randomStart{false};

	// The image showing `frames` logic frames after it started on its first (Anim2D::reset: the first image, the last
	// for the backwards modes).
	std::size_t ImageAt(std::uint64_t frames) const noexcept
	{
		const bool backwards = mode == Anim2DMode::OnceBackwards || mode == Anim2DMode::LoopBackwards || mode == Anim2DMode::PingPongBackwards;
		return ImageFrom(frames, backwards && !images.empty() ? images.size() - 1 : 0);
	}

	// The image it starts on (Anim2D::Anim2D): with RandomizeStartFrame randomizeCurrentFrame's
	// GameClientRandomValue(0, images - 1) from the client random draw `roll` (GetGameClientRandomValue: roll % (hi - lo +
	// 1) + lo, hi when lo >= hi), else reset's.
	std::size_t StartImage(std::uint32_t roll) const noexcept
	{
		if (!randomStart || images.empty())
			return ImageAt(0);
		return images.size() <= 1 ? images.size() - 1 : static_cast<std::size_t>(roll % static_cast<std::uint32_t>(images.size()));
	}

	// The image showing `frames` logic frames after it started on image `first` (Anim2D::tryNextFrame: one image on per
	// AnimationDelay; the ping-pongs, either way, first go up, as their reversed status starts clear).
	std::size_t ImageFrom(std::uint64_t frames, std::size_t first) const noexcept
	{
		const std::size_t count = images.size();
		if (count == 0)
			return 0;
		const std::size_t last = count - 1;
		first = std::min(first, last);
		const std::uint64_t step = frames / std::max<std::uint32_t>(framesPerImage, 1);
		switch (mode)
		{
		case Anim2DMode::Once:
			return static_cast<std::size_t>(std::min<std::uint64_t>(first + step, last));
		case Anim2DMode::OnceBackwards:
			return step >= first ? 0 : first - static_cast<std::size_t>(step);
		case Anim2DMode::Loop:
			return static_cast<std::size_t>((first + step) % count);
		case Anim2DMode::LoopBackwards:
			return static_cast<std::size_t>((first + count - step % count) % count);
		case Anim2DMode::PingPong:
		case Anim2DMode::PingPongBackwards: {
			if (last == 0)
				return 0;
			const std::uint64_t phase = (first + step) % (2 * last);
			return static_cast<std::size_t>(phase <= last ? phase : 2 * last - phase);
		}
		case Anim2DMode::None:
			break;
		}
		return 0;
	}
};

using Anim2DTemplates = std::map<std::string, Anim2DTemplate, std::less<>>;

inline Anim2DTemplates BindAnim2DTemplates(const engine::config::Document &document, std::uint64_t ticksPerSecond)
{
	Anim2DTemplates templates;
	for (const engine::config::Node &root : document.Roots())
	{
		if (root.key != "Animation" || root.values.empty())
			continue;
		Anim2DTemplate animation;
		std::int64_t count = 0;
		for (const engine::config::Node &field : root.children)
		{
			if (field.key == "NumberImages")
				count = engine::config::values::ParseInt(field.Value()).value_or(0);
			else if (field.key == "Image")
				animation.images.emplace_back(field.Value());
			else if (field.key == "ImageSequence")
				for (std::int64_t index = 0; index < count; ++index)
				{
					char suffix[8];
					std::snprintf(suffix, sizeof(suffix), "%03d", static_cast<int>(index));
					animation.images.push_back(std::string(field.Value()) + suffix);
				}
			else if (field.key == "AnimationMode")
			{
				constexpr std::array<std::string_view, 7> names{"NONE", "ONCE", "ONCE_BACKWARDS", "LOOP", "LOOP_BACKWARDS", "PING_PONG", "PING_PONG_BACKWARDS"};
				for (std::size_t index = 0; index < names.size(); ++index)
					if (field.Value() == names[index])
						animation.mode = static_cast<Anim2DMode>(index);
			}
			else if (field.key == "AnimationDelay")
			{
				const std::int64_t ms = engine::config::values::ParseInt(field.Value()).value_or(0);
				animation.framesPerImage = static_cast<std::uint32_t>(
					std::max<std::int64_t>((ms * static_cast<std::int64_t>(ticksPerSecond) + 999) / 1000, 1));
			}
			else if (field.key == "RandomizeStartFrame")
				animation.randomStart = engine::config::values::ParseBool(field.Value()).value_or(false);
		}
		templates[std::string(root.Value())] = std::move(animation);
	}
	return templates;
}
}
