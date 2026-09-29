export module engine.filesystem.core.path;
import std;

export namespace engine::filesystem
{
// Canonical virtual path: lower-case ASCII, '/' separators, no leading "./"
// or '/', no repeated separators. Game data refers to files case-insensitively
// with either slash ("Data\INI\Weapon.ini"), so every lookup goes through this.
inline std::string NormalizePath(std::string_view path)
{
	std::string out;
	out.reserve(path.size());
	for (const char raw : path)
	{
		char c = raw == '\\' ? '/' : raw;
		if (c >= 'A' && c <= 'Z')
			c = static_cast<char>(c - 'A' + 'a');
		if (c == '/' && (out.empty() || out.back() == '/'))
			continue;
		out += c;
	}
	while (out.starts_with("./"))
		out.erase(0, 2);
	return out;
}

inline std::string_view Extension(std::string_view normalizedPath) noexcept
{
	const auto dot = normalizedPath.rfind('.');
	const auto slash = normalizedPath.rfind('/');
	if (dot == std::string_view::npos || (slash != std::string_view::npos && dot < slash))
		return {};
	return normalizedPath.substr(dot);
}
}
