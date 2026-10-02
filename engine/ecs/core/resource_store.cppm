export module engine.ecs.core.resource_store;
import std;

// World-owned resources: shared data that is not per entity (the terrain,
// the spatial index, catalogs, settings, the tick's event channels, random
// seeds), one value per type. Systems hold no data; they reach resources
// through their SystemContext, which checks the access they declared
// (`using Resources = ecs::Resources<ecs::Read<A>, ecs::Write<B>>`) so the
// scheduler can order conflicting writers. The composition root inserts
// them. Stored by the stable name's key, in key order (deterministic).
export namespace ecs
{
template<typename T>
struct ResourceTraits;

using ResourceKey = std::uint64_t;

constexpr ResourceKey HashResourceKey(const std::string_view stableName) noexcept
{
	ResourceKey hash = 14695981039346656037ull;
	for (const char character : stableName)
	{
		hash ^= static_cast<ResourceKey>(static_cast<std::uint8_t>(character));
		hash *= 1099511628211ull;
	}
	return hash;
}

template<typename T>
concept ResourceType = requires { { ResourceTraits<T>::StableName } -> std::convertible_to<std::string_view>; };

template<ResourceType T>
constexpr ResourceKey ResourceKeyOf() noexcept
{
	return HashResourceKey(ResourceTraits<T>::StableName);
}

class ResourceStore
{
public:
	ResourceStore() = default;
	ResourceStore(const ResourceStore &) = delete;
	ResourceStore &operator=(const ResourceStore &) = delete;
	ResourceStore(ResourceStore &&) noexcept = default;
	ResourceStore &operator=(ResourceStore &&) noexcept = default;

	// Inserts T built from `args`; a type is inserted once.
	template<ResourceType T, typename... Args>
	T &Emplace(Args &&...args)
	{
		const ResourceKey key = ResourceKeyOf<T>();
		const auto at = LowerBound(key);
		if (at != m_slots.end() && at->key == key)
			throw std::logic_error("ECS resource '" + std::string(ResourceTraits<T>::StableName) + "' is already in the world");
		T *value = new T(std::forward<Args>(args)...);
		m_slots.insert(at, Slot{key, ResourceTraits<T>::StableName, Owned(value, [](void *pointer) { delete static_cast<T *>(pointer); })});
		return *value;
	}

	template<ResourceType T>
	T *Find() noexcept
	{
		const auto at = LowerBound(ResourceKeyOf<T>());
		return at != m_slots.end() && at->key == ResourceKeyOf<T>() ? static_cast<T *>(at->value.get()) : nullptr;
	}

	template<ResourceType T>
	const T *Find() const noexcept
	{
		return const_cast<ResourceStore *>(this)->Find<T>();
	}

	template<ResourceType T>
	T &Get()
	{
		if (T *value = Find<T>())
			return *value;
		throw std::logic_error("ECS resource '" + std::string(ResourceTraits<T>::StableName) + "' is not in the world");
	}

	template<ResourceType T>
	const T &Get() const
	{
		return const_cast<ResourceStore *>(this)->Get<T>();
	}

	template<ResourceType T>
	bool Contains() const noexcept
	{
		return Find<T>() != nullptr;
	}

	std::size_t Size() const noexcept { return m_slots.size(); }

private:
	using Owned = std::unique_ptr<void, void (*)(void *)>;

	struct Slot
	{
		ResourceKey key{};
		std::string_view stableName;
		Owned value;
	};

	std::vector<Slot>::iterator LowerBound(ResourceKey key)
	{
		return std::lower_bound(m_slots.begin(), m_slots.end(), key, [](const Slot &slot, ResourceKey wanted) { return slot.key < wanted; });
	}

	std::vector<Slot> m_slots;
};
}
