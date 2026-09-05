module;

#include <cstdint>
#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

export module Assets.Identity;

namespace Assets
{

export enum class AssetType : std::uint8_t
{
	Model,
	Texture,
	Material,
	Mesh,
	Skeleton,
	Animation,
	Font
};

export struct AssetIdentity final
{
	AssetType type = AssetType::Model;
	std::string canonical_name;

	friend bool operator==(const AssetIdentity &left, const AssetIdentity &right) noexcept
	{
		return left.type == right.type && left.canonical_name == right.canonical_name;
	}
};

export std::string Canonicalize_Asset_Name(std::string_view name);

export struct AssetDependencyDesc final
{
	AssetType type = AssetType::Model;
	std::string name;
};

export struct AssetDependency final
{
	AssetType type = AssetType::Model;
	AssetIdentity identity;
};

}

namespace Assets
{

std::string Canonicalize_Asset_Name(std::string_view name)
{
	std::string normalized;
	normalized.reserve(name.size());
	for (const char character : name) {
		if (character == '\\') {
			normalized.push_back('/');
		} else if (character >= 'A' && character <= 'Z') {
			normalized.push_back(static_cast<char>(character - 'A' + 'a'));
		} else {
			normalized.push_back(character);
		}
	}

	std::vector<std::string> components;
	std::size_t component_start = 0;
	while (component_start <= normalized.size()) {
		const std::size_t separator = normalized.find('/', component_start);
		const std::size_t component_end = separator == std::string::npos
			? normalized.size()
			: separator;
		const std::string_view component(normalized.data() + component_start, component_end - component_start);

		if (component.empty() || component == ".") {
			// Repeated separators and current-directory components do not affect
			// asset identity.
		} else if (component == "..") {
			if (!components.empty())
				components.pop_back();
		} else {
			components.emplace_back(component);
		}

		if (separator == std::string::npos)
			break;
		component_start = separator + 1;
	}

	std::string result;
	for (const std::string &component : components) {
		if (!result.empty())
			result.push_back('/');
		result += component;
	}
	return result;
}

}
