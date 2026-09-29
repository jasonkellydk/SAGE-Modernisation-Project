export module engine.config.binding.schema;
import std;

export import engine.config.binding.values;

export namespace engine::config
{
// Declarative binding of a block's children onto a plain struct:
//
//   const auto weapon = Schema<WeaponDefinition>{}
//       .Fixed("PrimaryDamage", &WeaponDefinition::damage)
//       .Duration("DelayBetweenShots", &WeaponDefinition::reloadTicks)
//       .On("DamageType", [](const Node &n, WeaponDefinition &w, BindContext &c) { ... });
//
// The struct stays plain data; the schema lives with the game's content code.
template<typename T>
class Schema
{
public:
	using Handler = std::function<void(const Node &, T &, BindContext &)>;

	Schema &On(std::string key, Handler handler)
	{
		m_handlers.insert_or_assign(std::move(key), std::move(handler));
		return *this;
	}

	// Keys that are recognised but intentionally not bound (e.g. editor-only).
	Schema &Ignore(std::string key)
	{
		return On(std::move(key), [](const Node &, T &, BindContext &) {});
	}

	template<typename Member>
	Schema &Integer(std::string key, Member T::*member)
	{
		return On(std::move(key), [member](const Node &node, T &out, BindContext &context) {
			if (const auto value = ReadInt(node, context))
				out.*member = static_cast<Member>(*value);
		});
	}

	Schema &Fixed(std::string key, math::Fixed T::*member)
	{
		return Assign(std::move(key), member, [](const Node &node, BindContext &context) { return ReadFixed(node, context); });
	}

	Schema &Boolean(std::string key, bool T::*member)
	{
		return Assign(std::move(key), member, [](const Node &node, BindContext &context) { return ReadBool(node, context); });
	}

	Schema &String(std::string key, std::string T::*member)
	{
		return Assign(std::move(key), member, [](const Node &node, BindContext &context) { return ReadString(node, context); });
	}

	Schema &Text(std::string key, std::string T::*member)
	{
		return On(std::move(key), [member](const Node &node, T &out, BindContext &) { out.*member = ReadText(node); });
	}

	Schema &StringList(std::string key, std::vector<std::string> T::*member)
	{
		return On(std::move(key), [member](const Node &node, T &out, BindContext &) { out.*member = ReadStringList(node); });
	}

	Schema &Percent(std::string key, math::Fixed T::*member)
	{
		return Assign(std::move(key), member, [](const Node &node, BindContext &context) { return ReadPercent(node, context); });
	}

	Schema &Duration(std::string key, std::uint64_t T::*member)
	{
		return Assign(std::move(key), member, [](const Node &node, BindContext &context) { return ReadDurationTicks(node, context); });
	}

	Schema &PerSecond(std::string key, math::Fixed T::*member)
	{
		return Assign(std::move(key), member, [](const Node &node, BindContext &context) { return ReadPerSecond(node, context); });
	}

	Schema &PerSecondSquared(std::string key, math::Fixed T::*member)
	{
		return Assign(std::move(key), member, [](const Node &node, BindContext &context) { return ReadPerSecondSquared(node, context); });
	}

	Schema &Degrees(std::string key, math::TurnAngle T::*member)
	{
		return Assign(std::move(key), member, [](const Node &node, BindContext &context) { return ReadDegrees(node, context); });
	}

	Schema &DegreesPerSecond(std::string key, math::TurnAngle T::*member)
	{
		return Assign(std::move(key), member, [](const Node &node, BindContext &context) { return ReadDegreesPerSecond(node, context); });
	}

	Schema &FixedVector3(std::string key, math::FixedVector3 T::*member)
	{
		return Assign(std::move(key), member, [](const Node &node, BindContext &context) { return ReadVec3(node, context); });
	}

	Schema &Color(std::string key, Rgb T::*member)
	{
		return Assign(std::move(key), member, [](const Node &node, BindContext &context) { return ReadRgb(node, context); });
	}

	template<typename E>
	Schema &Enum(std::string key, E T::*member, std::span<const EnumName<E>> names)
	{
		return On(std::move(key), [member, names](const Node &node, T &out, BindContext &context) {
			if (const auto value = ReadEnum<E>(node, context, names))
				out.*member = *value;
		});
	}

	// One nested block bound into a member.
	template<typename Sub>
	Schema &Block(std::string key, Sub T::*member, Schema<Sub> schema)
	{
		return On(std::move(key), [member, schema = std::move(schema)](const Node &node, T &out, BindContext &context) {
			if (RequireBlock(node, context))
				schema.Bind(node, out.*member, context);
		});
	}

	// Every occurrence of a nested block appended to a vector.
	template<typename Sub>
	Schema &Blocks(std::string key, std::vector<Sub> T::*member, Schema<Sub> schema)
	{
		return On(std::move(key), [member, schema = std::move(schema)](const Node &node, T &out, BindContext &context) {
			if (!RequireBlock(node, context))
				return;
			Sub value{};
			schema.Bind(node, value, context);
			(out.*member).push_back(std::move(value));
		});
	}

	bool Knows(std::string_view key) const { return m_handlers.contains(key); }

	void Bind(const Node &block, T &out, BindContext &context) const
	{
		for (const Node &child : block.children)
		{
			const auto handler = m_handlers.find(child.key);
			if (handler != m_handlers.end())
			{
				handler->second(child, out, context);
				continue;
			}
			std::string message = "unknown key '" + std::string(child.key) + "' in '" + std::string(block.key) + "'";
			if (context.unknownKeysAreErrors)
				context.diagnostics.Error(child.location, std::move(message));
			else
				context.diagnostics.Warning(child.location, std::move(message));
		}
	}

private:
	template<typename Member, typename Reader>
	Schema &Assign(std::string key, Member T::*member, Reader reader)
	{
		return On(std::move(key), [member, reader](const Node &node, T &out, BindContext &context) {
			if (auto value = reader(node, context))
				out.*member = std::move(*value);
		});
	}

	static bool RequireBlock(const Node &node, BindContext &context)
	{
		if (node.block)
			return true;
		context.diagnostics.Error(node.location, "'" + std::string(node.key) + "' must be a block ending with End");
		return false;
	}

	std::map<std::string, Handler, std::less<>> m_handlers;
};

// Named definitions of one kind, sorted by name so iteration order (and
// therefore anything derived from it, like dense ids) is deterministic.
template<typename T>
class DefinitionTable
{
public:
	T *Find(std::string_view name) noexcept
	{
		const auto found = m_entries.find(name);
		return found == m_entries.end() ? nullptr : &found->second;
	}

	const T *Find(std::string_view name) const noexcept
	{
		const auto found = m_entries.find(name);
		return found == m_entries.end() ? nullptr : &found->second;
	}

	T &Define(std::string name) { return m_entries[std::move(name)]; }
	bool Contains(std::string_view name) const { return m_entries.contains(name); }
	std::size_t Size() const noexcept { return m_entries.size(); }
	auto begin() const noexcept { return m_entries.begin(); }
	auto end() const noexcept { return m_entries.end(); }

private:
	std::map<std::string, T, std::less<>> m_entries;
};

enum class Redefinition
{
	Error,   // a name may be defined once
	Replace, // a later block replaces the earlier definition entirely
	Merge    // a later block overrides only the keys it sets
};

// Binds every top-level block named `blockType` ("Weapon Foo ... End") into
// `table`, keyed by the block's first value.
template<typename T>
void BindBlocks(const Document &document, std::string_view blockType, const Schema<T> &schema,
	DefinitionTable<T> &table, BindContext &context, Redefinition redefinition = Redefinition::Error)
{
	for (const Node &root : document.Roots())
	{
		if (root.key != blockType)
			continue;
		const std::string_view name = root.Value();
		if (name.empty())
		{
			context.diagnostics.Error(root.location, "'" + std::string(blockType) + "' block needs a name");
			continue;
		}
		if (table.Contains(name))
		{
			if (redefinition == Redefinition::Error)
			{
				context.diagnostics.Error(root.location, "'" + std::string(name) + "' is already defined");
				continue;
			}
			if (redefinition == Redefinition::Replace)
				table.Define(std::string(name)) = T{};
		}
		schema.Bind(root, table.Define(std::string(name)), context);
	}
}
}
