export module engine.config.binding.values;
import std;

export import engine.config.document.document;
export import engine.config.document.diagnostics;
export import Engine.Core.Math.Fixed;
export import Engine.Core.Math.FixedAngle;
export import Engine.Core.Math.FixedVector;
export import engine.time.simulation_time;

export namespace engine::config
{
namespace math = Engine::Math;

// What value parsing needs besides the node: where to report problems and
// the simulation step for per-second/millisecond conversions. Injected by
// whoever binds; there is no global game data.
struct BindContext
{
	Diagnostics &diagnostics;
	time::FixedStep step;
	// Unknown keys are warnings by default: shipped and modded data contain
	// fields the port does not read yet.
	bool unknownKeysAreErrors{false};
};

struct Rgb
{
	std::uint8_t r{0};
	std::uint8_t g{0};
	std::uint8_t b{0};
	constexpr bool operator==(const Rgb &) const noexcept = default;
};

template<typename E>
struct EnumName
{
	std::string_view name;
	E value;
};
}

namespace engine::config::detail
{
inline bool EqualsIgnoreCase(std::string_view a, std::string_view b) noexcept
{
	if (a.size() != b.size())
		return false;
	for (std::size_t index = 0; index < a.size(); ++index)
	{
		const auto lower = [](char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c; };
		if (lower(a[index]) != lower(b[index]))
			return false;
	}
	return true;
}

inline std::string Quote(std::string_view text) { return "'" + std::string(text) + "'"; }

// Splits "X:1.5 Y:-2" style text into label/value pairs.
inline std::vector<std::pair<std::string_view, std::string_view>> LabeledPairs(std::string_view text)
{
	std::vector<std::string_view> tokens;
	std::size_t index = 0;
	const auto separator = [](char c) { return c == ' ' || c == '\t' || c == '=' || c == ':'; };
	while (index < text.size())
	{
		while (index < text.size() && separator(text[index]))
			++index;
		const std::size_t start = index;
		while (index < text.size() && !separator(text[index]))
			++index;
		if (index > start)
			tokens.push_back(text.substr(start, index - start));
	}
	std::vector<std::pair<std::string_view, std::string_view>> pairs;
	for (std::size_t token = 0; token + 1 < tokens.size(); token += 2)
		pairs.emplace_back(tokens[token], tokens[token + 1]);
	return pairs;
}
}

export namespace engine::config::values
{
inline std::optional<std::int64_t> ParseInt(std::string_view token) noexcept
{
	if (!token.empty() && token.front() == '+')
		token.remove_prefix(1);
	std::int64_t value = 0;
	const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
	if (error != std::errc{} || end != token.data() + token.size())
		return std::nullopt;
	return value;
}

// Decimal text, exact to 2^-16. A trailing 'f' (C-style float suffix found in
// some shipped data) is accepted.
inline std::optional<math::Fixed> ParseFixed(std::string_view token) noexcept
{
	if (!token.empty() && (token.back() == 'f' || token.back() == 'F'))
		token.remove_suffix(1);
	return math::Fixed::ParseDecimal(token);
}

inline std::optional<bool> ParseBool(std::string_view token) noexcept
{
	for (const std::string_view yes : {"yes", "true", "1"})
		if (detail::EqualsIgnoreCase(token, yes))
			return true;
	for (const std::string_view no : {"no", "false", "0"})
		if (detail::EqualsIgnoreCase(token, no))
			return false;
	return std::nullopt;
}
}

export namespace engine::config
{
// ---- node readers: report a diagnostic and return nullopt on bad input ----

inline std::optional<std::string_view> RequireToken(const Node &node, BindContext &context, std::size_t index = 0)
{
	if (index < node.values.size())
		return node.values[index];
	context.diagnostics.Error(node.location, detail::Quote(node.key) + " needs a value");
	return std::nullopt;
}

inline std::optional<std::int64_t> ReadInt(const Node &node, BindContext &context)
{
	const auto token = RequireToken(node, context);
	if (!token)
		return std::nullopt;
	const auto value = values::ParseInt(*token);
	if (!value)
		context.diagnostics.Error(node.location, detail::Quote(node.key) + ": " + detail::Quote(*token) + " is not an integer");
	return value;
}

inline std::optional<math::Fixed> ReadFixed(const Node &node, BindContext &context, std::size_t index = 0)
{
	const auto token = RequireToken(node, context, index);
	if (!token)
		return std::nullopt;
	if (const auto value = values::ParseFixed(*token))
		return value;
	// Classic readers used sscanf("%f"), which reads the longest numeric
	// prefix ("40v.0" is 40, "18.5.0" is 18.5); shipped data relies on it.
	std::size_t length = 0;
	if (length < token->size() && ((*token)[length] == '-' || (*token)[length] == '+'))
		++length;
	bool point = false;
	while (length < token->size() && (((*token)[length] >= '0' && (*token)[length] <= '9') || ((*token)[length] == '.' && !point)))
	{
		if ((*token)[length] == '.')
			point = true;
		++length;
	}
	if (const auto prefix = values::ParseFixed(token->substr(0, length)))
	{
		context.diagnostics.Warning(node.location, detail::Quote(node.key) + ": " + detail::Quote(*token) +
			" read as " + std::string(token->substr(0, length)));
		return prefix;
	}
	context.diagnostics.Error(node.location, detail::Quote(node.key) + ": " + detail::Quote(*token) + " is not a number");
	return std::nullopt;
}

inline std::optional<bool> ReadBool(const Node &node, BindContext &context)
{
	const auto token = RequireToken(node, context);
	if (!token)
		return std::nullopt;
	const auto value = values::ParseBool(*token);
	if (!value)
		context.diagnostics.Error(node.location, detail::Quote(node.key) + ": " + detail::Quote(*token) + " is not Yes/No");
	return value;
}

// First token only (legacy AsciiString fields).
inline std::optional<std::string> ReadString(const Node &node, BindContext &context)
{
	const auto token = RequireToken(node, context);
	return token ? std::optional<std::string>(std::string(*token)) : std::nullopt;
}

// Whole value text with surrounding quotes removed (legacy quoted strings).
inline std::string ReadText(const Node &node)
{
	std::string_view text = node.text;
	if (text.size() >= 2 && text.front() == '"' && text.back() == '"')
		text = text.substr(1, text.size() - 2);
	return std::string(text);
}

inline std::vector<std::string> ReadStringList(const Node &node)
{
	return {node.values.begin(), node.values.end()};
}

// "50%" or "50" -> 0.5.
inline std::optional<math::Fixed> ReadPercent(const Node &node, BindContext &context)
{
	const auto token = RequireToken(node, context);
	if (!token)
		return std::nullopt;
	std::string_view number = *token;
	if (!number.empty() && number.back() == '%')
		number.remove_suffix(1);
	const auto value = values::ParseFixed(number);
	if (!value)
	{
		context.diagnostics.Error(node.location, detail::Quote(node.key) + ": " + detail::Quote(*token) + " is not a percentage");
		return std::nullopt;
	}
	return *value / math::Fixed::FromInt(100);
}

// Milliseconds -> whole simulation ticks, rounded up so nothing fires early.
inline std::optional<std::uint64_t> ReadDurationTicks(const Node &node, BindContext &context)
{
	const auto milliseconds = ReadFixed(node, context);
	if (!milliseconds)
		return std::nullopt;
	if (*milliseconds < math::Fixed{})
	{
		context.diagnostics.Error(node.location, detail::Quote(node.key) + ": duration cannot be negative");
		return std::nullopt;
	}
	// ceil(ms * rate / 1000), exact on the raw fixed-point value.
	const auto numerator = static_cast<std::uint64_t>(milliseconds->Raw()) * context.step.TicksPerSecond();
	const std::uint64_t denominator = std::uint64_t{1000} << math::Fixed::FractionBits;
	return (numerator + denominator - 1) / denominator;
}

// Distance per second -> distance per tick.
inline std::optional<math::Fixed> ReadPerSecond(const Node &node, BindContext &context)
{
	const auto perSecond = ReadFixed(node, context);
	return perSecond ? std::optional(context.step.PerTick(*perSecond)) : std::nullopt;
}

// Distance per second squared -> distance per tick squared.
inline std::optional<math::Fixed> ReadPerSecondSquared(const Node &node, BindContext &context)
{
	const auto perSecond = ReadFixed(node, context);
	return perSecond ? std::optional(context.step.PerTick(context.step.PerTick(*perSecond))) : std::nullopt;
}

inline std::optional<math::TurnAngle> ReadDegrees(const Node &node, BindContext &context)
{
	const auto degrees = ReadFixed(node, context);
	return degrees ? std::optional(math::TurnFromDegrees(*degrees)) : std::nullopt;
}

// Degrees per second -> rotation per tick.
inline std::optional<math::TurnAngle> ReadDegreesPerSecond(const Node &node, BindContext &context)
{
	const auto degrees = ReadFixed(node, context);
	return degrees ? std::optional(math::TurnFromDegrees(context.step.PerTick(*degrees))) : std::nullopt;
}

// "X:1 Y:2 Z:3" with any subset of labels; missing labels stay zero.
inline std::optional<math::FixedVector3> ReadVec3(const Node &node, BindContext &context)
{
	math::FixedVector3 result;
	for (const auto &[label, token] : detail::LabeledPairs(node.text))
	{
		const auto value = values::ParseFixed(token);
		math::Fixed *target = detail::EqualsIgnoreCase(label, "X") ? &result.x :
			detail::EqualsIgnoreCase(label, "Y") ? &result.y :
			detail::EqualsIgnoreCase(label, "Z") ? &result.z : nullptr;
		if (!value || target == nullptr)
		{
			context.diagnostics.Error(node.location, detail::Quote(node.key) + ": expected X:<n> Y:<n> Z:<n>");
			return std::nullopt;
		}
		*target = *value;
	}
	return result;
}

// "R:255 G:128 B:0" (an optional A: label is ignored).
inline std::optional<Rgb> ReadRgb(const Node &node, BindContext &context)
{
	Rgb color;
	for (const auto &[label, token] : detail::LabeledPairs(node.text))
	{
		const auto value = values::ParseInt(token);
		std::uint8_t *target = detail::EqualsIgnoreCase(label, "R") ? &color.r :
			detail::EqualsIgnoreCase(label, "G") ? &color.g :
			detail::EqualsIgnoreCase(label, "B") ? &color.b : nullptr;
		if (detail::EqualsIgnoreCase(label, "A"))
			continue;
		if (!value || target == nullptr || *value < 0 || *value > 255)
		{
			context.diagnostics.Error(node.location, detail::Quote(node.key) + ": expected R:<0-255> G:<0-255> B:<0-255>");
			return std::nullopt;
		}
		*target = static_cast<std::uint8_t>(*value);
	}
	return color;
}

// Case-insensitive name -> enum value.
template<typename E>
std::optional<E> ReadEnum(const Node &node, BindContext &context, std::span<const EnumName<E>> names, std::size_t index = 0)
{
	const auto token = RequireToken(node, context, index);
	if (!token)
		return std::nullopt;
	for (const EnumName<E> &entry : names)
		if (detail::EqualsIgnoreCase(entry.name, *token))
			return entry.value;
	context.diagnostics.Error(node.location, detail::Quote(node.key) + ": unknown value " + detail::Quote(*token));
	return std::nullopt;
}

// Space-separated flag names -> bit mask over `bits` (bit index = position in
// `names`). "NONE" clears, "+NAME"/"-NAME" adjust `current` instead of
// replacing it, matching how overriding data adds or removes single flags.
template<std::size_t Words>
std::optional<std::array<std::uint64_t, Words>> ReadFlags(const Node &node, BindContext &context,
	std::span<const std::string_view> names, std::array<std::uint64_t, Words> current = {})
{
	std::array<std::uint64_t, Words> result{};
	bool relative = false;
	for (const std::string_view raw : node.values)
		if (!raw.empty() && (raw.front() == '+' || raw.front() == '-'))
			relative = true;
	if (relative)
		result = current;
	for (std::string_view token : node.values)
	{
		bool remove = false;
		if (!token.empty() && (token.front() == '+' || token.front() == '-'))
		{
			remove = token.front() == '-';
			token.remove_prefix(1);
		}
		if (detail::EqualsIgnoreCase(token, "NONE"))
		{
			result = {};
			continue;
		}
		std::size_t bit = names.size();
		for (std::size_t index = 0; index < names.size(); ++index)
			if (detail::EqualsIgnoreCase(names[index], token))
				bit = index;
		if (bit == names.size() || bit >= Words * 64)
		{
			context.diagnostics.Error(node.location, detail::Quote(node.key) + ": unknown flag " + detail::Quote(token));
			return std::nullopt;
		}
		const std::uint64_t mask = std::uint64_t{1} << (bit % 64);
		if (remove)
			result[bit / 64] &= ~mask;
		else
			result[bit / 64] |= mask;
	}
	return result;
}
}
