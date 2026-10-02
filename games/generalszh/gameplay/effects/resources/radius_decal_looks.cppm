export module games.generalszh.gameplay.effects.resources.radius_decal_looks;
import std;

export import games.generalszh.content.global.radius_decal;
import engine.ecs.system.system;

// Every RadiusDecalTemplate the game's objects may lay (the AttackNuggets', the payload runs', the neutron missiles'),
// each once, in content order: what a RadiusDecal's look indexes. Made from content when a session starts (the same
// content: the same table), never changed after.
export namespace generalszh::gameplay
{
struct RadiusDecalLooks
{
	std::vector<content::RadiusDecalLook> looks;

	// Its index; appended when new (none for a template with no texture: RadiusDecalTemplate makes no decal).
	std::uint32_t Intern(const content::RadiusDecalLook &look)
	{
		if (!look.Present())
			return 0xFFFFFFFFu;
		const auto found = std::ranges::find(looks, look);
		if (found != looks.end())
			return static_cast<std::uint32_t>(found - looks.begin());
		looks.push_back(look);
		return static_cast<std::uint32_t>(looks.size() - 1);
	}
	std::uint32_t Find(const content::RadiusDecalLook &look) const noexcept
	{
		const auto found = std::ranges::find(looks, look);
		return look.Present() && found != looks.end() ? static_cast<std::uint32_t>(found - looks.begin()) : 0xFFFFFFFFu;
	}
	const content::RadiusDecalLook *At(std::uint32_t look) const noexcept { return look < looks.size() ? &looks[look] : nullptr; }
};
}

export namespace ecs
{
template<>
struct ResourceTraits<generalszh::gameplay::RadiusDecalLooks>
{
	static constexpr std::string_view StableName = "generalszh.gameplay.radius_decal_looks";
};
}
