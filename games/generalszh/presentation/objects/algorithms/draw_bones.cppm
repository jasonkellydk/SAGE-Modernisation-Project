export module games.generalszh.presentation.objects.algorithms.draw_bones;
import std;

export import games.generalszh.presentation.objects.resources.presentation_resources;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.resources.object_frame;
export import games.generalszh.presentation.objects.components.object_presentation;

// A bone of an object as its drawable finds it (Drawable::getPristineBoneData / getCurrentClientBoneTransforms): each of
// its draw modules in turn, its own model first and then its other W3DModelDraws (a particle cannon uplink's dish, whose
// ExtraPublicBones FXConnector and FXMain the uplink's lasers start from), the first that has it. Pristine: in the model
// of the state its conditions pick, at rest (getPristineBoneTransforms); current: as it is drawn now, animated
// (getCurrentClientBoneTransforms). Not ready while a model it would look in is still loading.
export namespace generalszh::presentation
{
struct DrawBoneSearch
{
	const BonePoses &poses;
	const LookCatalog &catalog;
	const DefinitionLooks &looks;
	const PresentedObject &object;
	const ExtraShownLooks *extras{nullptr}; // its other draws' states (none yet: its own model only)
	double clock{0.0};                      // the presentation clock (the extras' animation times)
};

// W3DModelDraw::doDrawModule with AttachToBoneInAnotherModule: the draw takes the bone's world transform (the object's
// world times the bone's transform in its model) in place of its own; row-major 4x4.
inline std::array<float, 16> AttachedDrawWorld(const std::array<float, 16> &world, const std::array<float, 12> &bone) noexcept
{
	std::array<float, 16> result{};
	for (std::size_t row = 0; row < 3; ++row)
		for (std::size_t column = 0; column < 4; ++column)
		{
			float value = column == 3 ? world[row * 4 + 3] : 0.0f;
			for (std::size_t k = 0; k < 3; ++k)
				value += world[row * 4 + k] * bone[k * 4 + column];
			result[row * 4 + column] = value;
		}
	result[15] = 1.0f;
	return result;
}

inline BoneLookup FindDrawBone(const DrawBoneSearch &search, std::string_view bone, bool current)
{
	const BonePoses &poses = search.poses;
	const auto modelOf = [&](std::uint32_t look) -> std::string_view {
		return look < search.catalog.lookModels.size() ? std::string_view(search.catalog.lookModels[look]) : std::string_view{};
	};
	const auto at = [&](std::uint32_t look, float seconds, float start) -> BoneLookup {
		if (current && poses.animated)
			return poses.animated(look, seconds, start, bone);
		const std::string_view model = modelOf(look);
		return poses.pose && !model.empty() ? poses.pose(model, bone) : BoneLookup{true, false};
	};
	BoneLookup found = at(search.object.look, search.object.animationSeconds, search.object.animationStart);
	if (!found.ready || found.found || search.extras == nullptr)
		return found;
	const std::size_t count = std::min(search.looks.extraDraws.size(), ExtraShownLooks::Capacity);
	for (std::size_t index = 0; index < count; ++index)
	{
		if ((search.extras->made & (1u << index)) == 0)
			continue;
		const DefinitionLooks::ExtraDraw &draw = search.looks.extraDraws[index];
		const ShownLook &shown = search.extras->draws[index];
		// Pristine: the look of the state its conditions pick (its first variant); current: the one showing.
		std::uint32_t look = shown.look;
		if (!current && shown.state < draw.stateLooks.size())
			look = draw.stateLooks[shown.state];
		if (modelOf(look).empty())
			continue;
		const double held = shown.held >= 0.0 ? shown.held : search.clock - shown.since;
		const BoneLookup there = at(look, static_cast<float>(held * shown.speed), shown.start);
		if (!there.ready)
			return there;
		if (there.found)
			return there;
	}
	return found;
}
}
