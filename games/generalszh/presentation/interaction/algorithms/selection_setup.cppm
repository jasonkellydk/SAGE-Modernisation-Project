export module games.generalszh.presentation.interaction.algorithms.selection_setup;
import std;

export import games.generalszh.presentation.interaction.resources.interaction_resources;
export import games.generalszh.session.session_view;
import Engine.Core.Math.FixedPresentation;
import games.generalszh.content.crates.crate_content;
import games.generalszh.content.combat.loadout_content;

// The selection catalog, filled for the definitions the session has taken on
// (the definitions it knows grow as objects of new kinds appear): the kinds
// selection cares about, and a pick sphere from the geometry (the bounding
// sphere, centred half way up a box or cylinder).
export namespace generalszh::presentation
{
inline void KnowSelectables(SelectionCatalog &catalog, const session::SessionView &game)
{
	const auto count = static_cast<std::uint32_t>(game.DefinitionCount());
	for (std::uint32_t index = static_cast<std::uint32_t>(catalog.byDefinition.size()); index < count; ++index)
	{
		const content::ObjectDefinition &object = game.Definition(index);
		SelectionLook look;
		const std::pair<std::string_view, std::uint16_t> kinds[] = {{"SELECTABLE", select_kind::Selectable}, {"ALWAYS_SELECTABLE", select_kind::AlwaysSelectable},
			{"FORCEATTACKABLE", select_kind::ForceAttackable}, {"STRUCTURE", select_kind::Structure}, {"INFANTRY", select_kind::Infantry},
			{"CRATE", select_kind::Crate}, {"MINE", select_kind::Mine}, {"SHRUBBERY", select_kind::Shrubbery}, {"IGNORED_IN_GUI", select_kind::IgnoredInGui},
			{"VEHICLE", select_kind::Vehicle}, {"AIRCRAFT", select_kind::Aircraft}, {"REPAIR_PAD", select_kind::RepairPad}};
		for (const auto &[name, bit] : kinds)
			if (object.Is(name))
				look.kinds |= bit;
		look.radius = std::max(1.0f, Engine::Math::ToFloat(content::BoundingSphereRadius(object.geometry)));
		look.center = object.geometry.shape == content::GeometryShape::Sphere ? 0.0f : Engine::Math::ToFloat(object.geometry.height) * 0.5f;
		const float major = Engine::Math::ToFloat(object.geometry.majorRadius), minor = Engine::Math::ToFloat(object.geometry.minorRadius);
		look.top = object.geometry.shape == content::GeometryShape::Sphere ? major : Engine::Math::ToFloat(object.geometry.height);
		look.healthBoxWidth = (look.kinds & select_kind::IgnoredInGui) != 0 ? 0.0f : std::max(20.0f, std::clamp(major + minor, 20.0f, 150.0f) * 2.0f);
		// ConvertToCarBombCrateCollide, and ConvertToCarBombCrateCollide::isValidToExecute's checks on the car's template.
		if (const auto collide = content::ReadCrateCollide(object); collide && collide->kind == content::CrateKind::CarBomb)
		{
			look.carBomber = static_cast<std::uint32_t>(catalog.carBombers.size());
			catalog.carBombers.push_back({collide->required, collide->forbidden});
		}
		if (const auto collide = content::ReadCrateCollide(object); collide && collide->kind == content::CrateKind::Hijack)
		{
			look.hijacker = static_cast<std::uint32_t>(catalog.hijackers.size());
			catalog.hijackers.push_back({collide->required, collide->forbidden});
		}
		const std::uint32_t carBomb = content::SetFlag(content::WeaponSetFlagNames, "CARBOMB");
		look.carBombable = game.DefinitionHasAi(index) && !object.Is("AIRCRAFT") && !object.Is("BOAT") &&
			std::ranges::any_of(content::ReadObjectLoadout(object).weaponSets, [&](const content::WeaponSetContent &set) { return (set.conditions & carBomb) != 0; });
		catalog.kinds.push_back(object.kinds);
		catalog.byDefinition.push_back(look);
	}
}
}
