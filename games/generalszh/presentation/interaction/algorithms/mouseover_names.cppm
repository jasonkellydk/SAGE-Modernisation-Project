export module games.generalszh.presentation.interaction.algorithms.mouseover_names;
import std;

export import games.generalszh.presentation.interaction.resources.mouse_tooltip;
export import games.generalszh.session.session_view;

// What the mouse-over tooltip names, by definition: as the session takes definitions on, each its DisplayName's text
// and its "ThingTemplate:<name>" text (InGameUI::createMouseoverHint's thingTemplate->getDisplayName() and its
// TheGameText->fetch fallback), from the texts fetched once (MouseoverNames' labelTexts / templateTexts).
export namespace generalszh::presentation
{
inline void KnowMouseoverNames(MouseoverNames &names, const session::SessionView &game)
{
	const auto count = static_cast<std::uint32_t>(game.DefinitionCount());
	for (auto index = static_cast<std::uint32_t>(names.displayNames.size()); index < count; ++index)
	{
		const content::ObjectDefinition &object = game.Definition(index);
		const auto label = names.labelTexts.find(object.displayName);
		names.displayNames.push_back(!object.displayName.empty() && label != names.labelTexts.end() ? label->second : std::u16string{});
		const auto fallback = names.templateTexts.find(object.name);
		names.templateNames.push_back(fallback != names.templateTexts.end() ? fallback->second : std::u16string{});
	}
}
}
