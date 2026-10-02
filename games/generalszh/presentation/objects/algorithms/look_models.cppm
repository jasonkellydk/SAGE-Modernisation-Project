export module games.generalszh.presentation.objects.algorithms.look_models;
import std;

export import games.generalszh.presentation.objects.resources.look_models;
export import games.generalszh.presentation.objects.resources.look_catalog;
export import games.generalszh.presentation.objects.resources.presentation_resources;

// A look's model (W3DModelDraw's condition state picked, or the model a debris piece shows): its model and animation,
// its hidden, shown and muzzle flash parts (with its SubObjectsUpgrade overrides in order), the bones its object turns
// (W3DTruckDraw tires, cab and trailer; Turret, TurretPitch and their art angles; recoil barrels; AltTurret) and its
// loaded projectiles (ProjectileBoneFeedbackEnabledSlots). A definition without condition states draws its default
// model (a map tree with its TextureName, as W3DTreeBuffer::updateTexture).
export namespace generalszh::presentation
{
inline float TurnRadians(std::uint32_t units) noexcept
{
	return static_cast<float>(static_cast<std::int32_t>(units)) * 6.283185307179586f / 4294967296.0f;
}

inline ObjectModel LookModelOf(const LookCatalog &catalog, const MotionLooks *motion, std::uint32_t look)
{
	if (look >= catalog.looks.size())
		return {};
	const LookEntry info = catalog.looks[look];
	// A model shown instead of a definition's (a debris piece), still or playing one of its animations.
	if (info.model != 0)
		return {catalog.lookModels[look], look < catalog.lookAnimations.size() ? catalog.lookAnimations[look] : std::string{},
			static_cast<ObjectAnimationMode>(info.mode), false};
	if (info.definition >= catalog.byDefinition.size())
		return {};
	const DefinitionLooks &definitionLooks = catalog.byDefinition[info.definition];
	if (info.draw != 0)
	{
		// Another draw module (a rider, a scaffold): its state's model, the animation of this look and parts, turning nothing
		// (its model states step as its own draw's: ExtraShownLooks).
		const content::ModelStates &extra = definitionLooks.extraDraws[info.draw - 1].states;
		const content::ModelState &picked = extra.states[info.state];
		return {picked.model, picked.animations.empty() ? std::string{} : picked.animations[std::min<std::size_t>(info.variant, picked.animations.size() - 1)],
			static_cast<ObjectAnimationMode>(picked.animationMode), false, picked.hiddenSubObjects, picked.shownSubObjects, picked.muzzleFlashes};
	}
	const content::ModelStates &states = definitionLooks.states;
	const MotionLook *wheels = motion != nullptr ? motion->Of(info.definition) : nullptr;
	std::vector<std::string> tires = wheels != nullptr ? wheels->wheelBones : std::vector<std::string>{};
	std::vector<std::string> steered = wheels != nullptr ? wheels->steeredBones : std::vector<std::string>{};
	const std::vector<std::uint8_t> corners = wheels != nullptr ? wheels->wheelCorners : std::vector<std::uint8_t>{};
	const std::string cab = wheels != nullptr ? wheels->cabBone : std::string{};
	const std::string trailer = wheels != nullptr ? wheels->trailerBone : std::string{};
	if (!states.Empty())
	{
		const content::ModelState &picked = states.states[info.state];
		const std::string animation = picked.animations.empty() ? std::string{} : picked.animations[std::min<std::size_t>(info.variant, picked.animations.size() - 1)];
		ObjectModel model{picked.model, animation, static_cast<ObjectAnimationMode>(picked.animationMode), false, picked.hiddenSubObjects,
			picked.shownSubObjects, picked.muzzleFlashes, std::move(tires), std::move(steered), cab, trailer, picked.turretBone, picked.turretPitchBone,
			TurnRadians(picked.turretArtAngle.units), TurnRadians(picked.turretArtPitch.units), picked.recoilBone, picked.altTurretBone,
			picked.altTurretPitchBone, TurnRadians(picked.altTurretArtAngle.units), TurnRadians(picked.altTurretArtPitch.units)};
		model.tireCorners = corners;
		// Its part overrides (SubObjectsUpgrade), in order over its state's own.
		if (info.parts != 0 && info.parts < catalog.partOverrides.size())
			for (const auto &[part, show] : catalog.partOverrides[info.parts])
			{
				const auto same = [&](const std::string &name) {
					return std::ranges::equal(name, part, [](char a, char b) {
						return std::tolower(static_cast<unsigned char>(a)) == std::tolower(static_cast<unsigned char>(b));
					});
				};
				std::erase_if(model.hidden, same);
				std::erase_if(model.shown, same);
				(show ? model.shown : model.hidden).push_back(part);
			}
		model.projectileSlots = states.projectileFeedbackSlots;
		model.projectileBones = picked.slotLaunchBones;
		model.projectileHideShow = picked.slotHideShowBones;
		return model;
	}
	// The content's AnimationMode names map one to one onto the scene's.
	const content::RestingModel &resting = definitionLooks.resting;
	ObjectModel model{resting.model, resting.animation, static_cast<ObjectAnimationMode>(resting.animationMode), resting.idleAnimation, {}, {}, {},
		std::move(tires), std::move(steered), cab, trailer};
	model.tireCorners = corners;
	// W3DTreeBuffer::updateTexture: a map tree is drawn with its TextureName, from Art/Terrain, else Art/Textures (the
	// asset source reads a path as given, then looks the file name up under the textures).
	if (definitionLooks.bufferTree && !definitionLooks.treeMotion.texture.empty())
		model.texture = "Art/Terrain/" + definitionLooks.treeMotion.texture;
	return model;
}
}
