export module engine.gameplay.common.identity.resources.template_equivalence;
import std;

import engine.ecs.system.system;

// Which kinds of object count as the same (ThingTemplate::isEquivalentTo): the same kind, one reskinned from the other,
// or both reskinned from the same kind. Definition data, indexed by DefinitionRef: each definition's own family key and
// that of the kind it was reskinned from (the game's ObjectReskin; None: not a reskin). Keys stand for kinds' names, so
// a kind named as a reskin's source matches whether or not it has a definition yet.
export namespace engine::gameplay
{
struct TemplateEquivalence
{
	static constexpr std::uint32_t None = 0xFFFFFFFFu;

	std::vector<std::uint32_t> self;
	std::vector<std::uint32_t> reskinnedFrom;

	bool Equivalent(std::uint32_t a, std::uint32_t b) const noexcept
	{
		if (a == b)
			return true;
		if (a >= self.size() || b >= self.size())
			return false;
		const std::uint32_t fromA = reskinnedFrom[a], fromB = reskinnedFrom[b];
		return self[a] == self[b] || fromA == self[b] || self[a] == fromB || (fromA != None && fromA == fromB);
	}
};
}

export namespace ecs
{
template<>
struct ResourceTraits<engine::gameplay::TemplateEquivalence>
{
	static constexpr std::string_view StableName = "engine.gameplay.template_equivalence";
};
}
