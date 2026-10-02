export module engine.ecs.core.component_registry;
import std;

export import engine.ecs.core.state_hash;
export import engine.core.serialization.byte_stream;
export import engine.ecs.storage.side_table;

export namespace ecs
{
    using ComponentId = std::uint32_t;
    using ComponentKey = std::uint64_t;
    using ComponentSchemaHash = std::uint64_t;

    inline constexpr ComponentId InvalidComponentId = (std::numeric_limits<ComponentId>::max)();
    inline constexpr ComponentSchemaHash UnfinalizedSchemaHash = 0;
    inline constexpr std::uint32_t ComponentSchemaFormatVersion = 1;

    enum class PersistencePolicy : std::uint8_t
    {
        Transient,
        Serializable
    };

    // Where a component's values live. Table: in the archetype chunks (the
    // default; simulation state). SideTable: in a per-type sparse set outside
    // the archetypes (presentation state): never moves entities, never part
    // of signatures, the schema hash, the state hash or checkpoints. Opt in
    // with `static constexpr ComponentStorage Storage = ComponentStorage::SideTable;`.
    enum class ComponentStorage : std::uint8_t
    {
        Table,
        SideTable
    };

    // Every component must provide an explicit specialization. StableName is
    // the canonical, engine-level name used to derive StableKey.
    template<typename T>
    struct ComponentTraits;

    constexpr ComponentKey HashComponentKey(const std::string_view stableName) noexcept
    {
        // FNV-1a is used only as a deterministic key function. Collisions are
        // validated and rejected by ComponentRegistry during registration.
        ComponentKey hash = 14695981039346656037ull;
        for (const char character : stableName)
        {
            hash ^= static_cast<ComponentKey>(static_cast<std::uint8_t>(character));
            hash *= 1099511628211ull;
        }
        return hash;
    }

    struct ComponentInfo
    {
        ComponentId id{ InvalidComponentId };
        ComponentKey stableKey{};
        std::string_view stableName{};
        std::uint32_t version{};
        PersistencePolicy persistence{ PersistencePolicy::Transient };
        ComponentStorage storage{ ComponentStorage::Table };
        // Side-table components: makes the (empty) table for a world.
        std::unique_ptr<SideTableBase> (*createSideTable)(){};
        std::size_t size{};
        std::size_t alignment{};

        // Null when the component intentionally has no default constructor.
        void (*constructDefault)(void*){};
        void (*constructMove)(void*, void*) noexcept{};
        void (*destroy)(void*) noexcept{};
        // Hashes `count` contiguous values for World::StateHash. Null for
        // Transient components, and for Serializable components that are
        // neither padding-free nor provide ComponentTraits<T>::HashState.
        void (*hashState)(const void*, std::size_t, StateHasher&) noexcept{};
        // Write / read `count` contiguous values for world checkpoints (read
        // overwrites default-constructed values). Null for Transient components,
        // and for Serializable ones neither trivially copyable nor providing
        // ComponentTraits<T>::Save / Load.
        void (*saveState)(const void*, std::size_t, engine::core::serialization::ByteWriter&){};
        bool (*loadState)(void*, std::size_t, engine::core::serialization::ByteReader&){};
        // Saved as its bytes though it has padding or floats: a checkpoint would hold whatever memory was in its
        // padding (two identical worlds saving different checkpoints), so World::SaveCheckpoint refuses it.
        bool savesPadding{false};
    };

    class ComponentRegistry
    {
    public:
        template<typename T>
        ComponentId Register();

        template<typename T>
        ComponentId TryGet() const noexcept;

        [[nodiscard]] ComponentId TryGet(const std::type_info& type) const noexcept;
        [[nodiscard]] const ComponentInfo* TryGet(ComponentId id) const noexcept;
        [[nodiscard]] ComponentId TryGet(ComponentKey key) const noexcept;
        [[nodiscard]] const ComponentInfo& Get(ComponentId id) const;

        // Finalization sorts all registered descriptors by stable key, assigns
        // dense runtime IDs, computes the schema hash, and freezes the set.
        void Finalize();

        [[nodiscard]] bool IsFrozen() const noexcept { return m_frozen; }
        [[nodiscard]] ComponentSchemaHash SchemaHash() const noexcept { return m_schemaHash; }
        [[nodiscard]] std::size_t Count() const noexcept { return m_infos.size(); }

    private:
        template<typename T>
        static void ConstructDefault(void* destination)
        {
            std::construct_at(static_cast<T*>(destination));
        }

        template<typename T>
        static void ConstructMove(void* destination, void* source) noexcept
        {
            std::construct_at(static_cast<T*>(destination), std::move(*static_cast<T*>(source)));
        }

        template<typename T>
        static void Destroy(void* object) noexcept
        {
            std::destroy_at(static_cast<T*>(object));
        }

        template<typename T>
        static void HashCustom(const void* values, std::size_t count, StateHasher& hasher) noexcept
        {
            const T* typed = static_cast<const T*>(values);
            for (std::size_t index = 0; index < count; ++index)
                ComponentTraits<T>::HashState(typed[index], hasher);
        }

        template<typename T>
        static void HashBytes(const void* values, std::size_t count, StateHasher& hasher) noexcept
        {
            hasher.AppendBytes(std::as_bytes(std::span{static_cast<const T*>(values), count}));
        }

        template<typename T>
        static void SaveCustom(const void* values, std::size_t count, engine::core::serialization::ByteWriter& writer)
        {
            const T* typed = static_cast<const T*>(values);
            for (std::size_t index = 0; index < count; ++index)
                ComponentTraits<T>::Save(typed[index], writer);
        }

        template<typename T>
        static bool LoadCustom(void* values, std::size_t count, engine::core::serialization::ByteReader& reader)
        {
            T* typed = static_cast<T*>(values);
            for (std::size_t index = 0; index < count; ++index)
                if (!ComponentTraits<T>::Load(typed[index], reader))
                    return false;
            return true;
        }

        template<typename T>
        static void SaveBytes(const void* values, std::size_t count, engine::core::serialization::ByteWriter& writer)
        {
            writer.Raw(std::as_bytes(std::span{static_cast<const T*>(values), count}));
        }

        template<typename T>
        static bool LoadBytes(void* values, std::size_t count, engine::core::serialization::ByteReader& reader)
        {
            return reader.Raw(std::as_writable_bytes(std::span{static_cast<T*>(values), count}));
        }

        static ComponentSchemaHash ComputeSchemaHash(const std::vector<ComponentInfo*>& ordered) noexcept;

        std::deque<ComponentInfo> m_infos;
        // RTTI is used only to deduplicate repeated registration of the same
        // C++ type. It is never used for stable identity, dense IDs, or schema.
        std::unordered_map<std::type_index, ComponentInfo*> m_typeToInfo;
        std::unordered_map<ComponentKey, ComponentInfo*> m_keyToInfo;
        std::vector<ComponentInfo*> m_idToInfo;
        // Once frozen: stable key -> dense id, open addressing (power-of-two size, linear probing). TryGet<T> of a
        // component type with traits hashes nothing at run time: its key is a compile-time constant, and a stable
        // name belongs to one registered type (duplicates are rejected), so the key alone identifies it.
        struct KeySlot
        {
            ComponentKey key{};
            ComponentId id{ InvalidComponentId };
        };
        std::vector<KeySlot> m_keySlots;
        std::size_t m_keyMask{ 0 };
        bool m_frozen{ false };
        ComponentSchemaHash m_schemaHash{ UnfinalizedSchemaHash };

    public:
        // The dense id of a stable key (InvalidComponentId: none, or not frozen yet).
        [[nodiscard]] ComponentId FindFrozenKey(const ComponentKey key) const noexcept
        {
            if (m_keySlots.empty())
                return InvalidComponentId;
            for (std::size_t slot = static_cast<std::size_t>(key ^ (key >> 29)) & m_keyMask;; slot = (slot + 1) & m_keyMask)
            {
                const KeySlot& entry = m_keySlots[slot];
                if (entry.id == InvalidComponentId || entry.key == key)
                    return entry.id;
            }
        }
    };

    namespace detail
    {
        template<typename T>
        concept HasComponentTraits = requires
        {
            { ComponentTraits<T>::StableName } -> std::convertible_to<std::string_view>;
            { ComponentTraits<T>::Version } -> std::convertible_to<std::uint32_t>;
            { ComponentTraits<T>::Persistence } -> std::convertible_to<PersistencePolicy>;
        };

        inline void AppendByte(ComponentSchemaHash& hash, const std::uint8_t value) noexcept
        {
            hash ^= static_cast<ComponentSchemaHash>(value);
            hash *= 1099511628211ull;
        }

        inline void AppendU32(ComponentSchemaHash& hash, const std::uint32_t value) noexcept
        {
            AppendByte(hash, static_cast<std::uint8_t>(value >> 24));
            AppendByte(hash, static_cast<std::uint8_t>(value >> 16));
            AppendByte(hash, static_cast<std::uint8_t>(value >> 8));
            AppendByte(hash, static_cast<std::uint8_t>(value));
        }

        inline void AppendU64(ComponentSchemaHash& hash, const std::uint64_t value) noexcept
        {
            AppendByte(hash, static_cast<std::uint8_t>(value >> 56));
            AppendByte(hash, static_cast<std::uint8_t>(value >> 48));
            AppendByte(hash, static_cast<std::uint8_t>(value >> 40));
            AppendByte(hash, static_cast<std::uint8_t>(value >> 32));
            AppendByte(hash, static_cast<std::uint8_t>(value >> 24));
            AppendByte(hash, static_cast<std::uint8_t>(value >> 16));
            AppendByte(hash, static_cast<std::uint8_t>(value >> 8));
            AppendByte(hash, static_cast<std::uint8_t>(value));
        }
    }

    template<typename T>
    ComponentId ComponentRegistry::Register()
    {
        static_assert(std::is_object_v<T>, "ECS components must be object types");
        static_assert(!std::is_const_v<T> && !std::is_volatile_v<T>, "ECS component types must be unqualified");
        static_assert(std::is_move_constructible_v<T>, "ECS components must be move constructible");
        static_assert(std::is_nothrow_move_constructible_v<T>, "ECS component moves must be noexcept");
        static_assert(std::is_destructible_v<T>, "ECS components must be destructible");
        static_assert(std::is_nothrow_destructible_v<T>, "ECS component destruction must be noexcept");
        static_assert(detail::HasComponentTraits<T>,
            "Every ECS component must specialize ecs::ComponentTraits with StableName, Version, and Persistence");

        const std::type_index type = std::type_index(typeid(T));
        if (const auto existing = m_typeToInfo.find(type); existing != m_typeToInfo.end())
        {
            return existing->second->id;
        }

        if (m_frozen)
        {
            throw std::logic_error("Cannot register a new ECS component after registry finalization");
        }

        const std::string_view stableName = ComponentTraits<T>::StableName;
        if (stableName.empty())
        {
            throw std::invalid_argument("ECS component stable name cannot be empty");
        }

        const ComponentKey stableKey = HashComponentKey(stableName);
        for (const ComponentInfo& existing : m_infos)
        {
            if (existing.stableKey == stableKey)
            {
                if (existing.stableName == stableName)
                {
                    throw std::logic_error("Duplicate ECS component stable key: " + std::string(stableName));
                }
                throw std::logic_error("ECS component stable-key hash collision");
            }
        }

        ComponentInfo info;
        info.stableKey = stableKey;
        info.stableName = stableName;
        info.version = static_cast<std::uint32_t>(ComponentTraits<T>::Version);
        info.persistence = ComponentTraits<T>::Persistence;
        if constexpr (requires { { ComponentTraits<T>::Storage } -> std::convertible_to<ComponentStorage>; })
        {
            if constexpr (ComponentTraits<T>::Storage == ComponentStorage::SideTable)
            {
                static_assert(ComponentTraits<T>::Persistence == PersistencePolicy::Transient,
                    "Side-table components are presentation state: they must be Transient");
                info.storage = ComponentStorage::SideTable;
                info.createSideTable = [] { return std::unique_ptr<SideTableBase>(std::make_unique<SideTable<T>>()); };
            }
        }
        info.size = sizeof(T);
        info.alignment = alignof(T);
        info.constructMove = &ConstructMove<T>;
        info.destroy = &Destroy<T>;
        if constexpr (std::is_default_constructible_v<T>)
        {
            info.constructDefault = &ConstructDefault<T>;
        }
        if (info.persistence == PersistencePolicy::Serializable)
        {
            // Byte hashing is only platform-independent without padding or
            // floating point, which has_unique_object_representations rules out.
            if constexpr (requires(const T& value, StateHasher& hasher) { ComponentTraits<T>::HashState(value, hasher); })
                info.hashState = &HashCustom<T>;
            else if constexpr (std::is_trivially_copyable_v<T> && std::has_unique_object_representations_v<T>)
                info.hashState = &HashBytes<T>;
            if constexpr (requires(const T& value, T& target, engine::core::serialization::ByteWriter& writer,
                              engine::core::serialization::ByteReader& reader) {
                              ComponentTraits<T>::Save(value, writer);
                              { ComponentTraits<T>::Load(target, reader) } -> std::convertible_to<bool>;
                          })
            {
                info.saveState = &SaveCustom<T>;
                info.loadState = &LoadCustom<T>;
            }
            else if constexpr (std::is_trivially_copyable_v<T>)
            {
                info.saveState = &SaveBytes<T>;
                info.loadState = &LoadBytes<T>;
                info.savesPadding = !std::has_unique_object_representations_v<T>;
            }
        }

        m_infos.push_back(info);
        try
        {
            m_typeToInfo.emplace(type, &m_infos.back());
            m_keyToInfo.emplace(stableKey, &m_infos.back());
        }
        catch (...)
        {
            m_keyToInfo.erase(stableKey);
            m_typeToInfo.erase(type);
            m_infos.pop_back();
            throw;
        }
        return InvalidComponentId;
    }

    template<typename T>
    ComponentId ComponentRegistry::TryGet() const noexcept
    {
        if constexpr (detail::HasComponentTraits<T>)
        {
            if (m_frozen)
            {
                constexpr ComponentKey key = HashComponentKey(ComponentTraits<T>::StableName);
                return FindFrozenKey(key);
            }
        }
        return TryGet(typeid(T));
    }
}

namespace ecs
{
    namespace
    {
        constexpr ComponentSchemaHash SchemaHashOffset = 14695981039346656037ull;

        void AppendSchemaTag(ComponentSchemaHash& hash) noexcept
        {
            constexpr std::string_view tag = "ECS_COMPONENT_SCHEMA";
            for (const char character : tag)
            {
                detail::AppendByte(hash, static_cast<std::uint8_t>(character));
            }
        }
    }

    ComponentSchemaHash ComponentRegistry::ComputeSchemaHash(const std::vector<ComponentInfo*>& ordered) noexcept
    {
        ComponentSchemaHash hash = SchemaHashOffset;
        AppendSchemaTag(hash);
        detail::AppendU32(hash, ComponentSchemaFormatVersion);
        detail::AppendU64(hash, static_cast<std::uint64_t>(ordered.size()));
        for (const ComponentInfo* info : ordered)
        {
            detail::AppendU64(hash, info->stableKey);
            detail::AppendU32(hash, info->version);
            detail::AppendByte(hash, static_cast<std::uint8_t>(info->persistence));
        }
        return hash;
    }

    const ComponentInfo* ComponentRegistry::TryGet(const ComponentId id) const noexcept
    {
        return static_cast<std::size_t>(id) < m_idToInfo.size() ? m_idToInfo[id] : nullptr;
    }

    ComponentId ComponentRegistry::TryGet(const std::type_info& type) const noexcept
    {
        const auto existing = m_typeToInfo.find(std::type_index(type));
        return existing == m_typeToInfo.end() ? InvalidComponentId : existing->second->id;
    }

    ComponentId ComponentRegistry::TryGet(const ComponentKey key) const noexcept
    {
        const auto found = m_keyToInfo.find(key);
        return found == m_keyToInfo.end() ? InvalidComponentId : found->second->id;
    }

    const ComponentInfo& ComponentRegistry::Get(const ComponentId id) const
    {
        const ComponentInfo* info = TryGet(id);
        if (info == nullptr)
        {
            throw std::out_of_range("Invalid ECS component ID");
        }
        return *info;
    }

    void ComponentRegistry::Finalize()
    {
        if (m_frozen)
        {
            return;
        }

        std::vector<ComponentInfo*> ordered;
        ordered.reserve(m_infos.size());
        for (ComponentInfo& info : m_infos)
        {
            ordered.push_back(&info);
        }

        // Table components take the first ids, so the simulation's ids, archetype
        // signatures and schema are the same whatever side-table (presentation)
        // components a peer registers.
        std::sort(ordered.begin(), ordered.end(), [](const ComponentInfo* left, const ComponentInfo* right)
        {
            if (left->storage != right->storage)
            {
                return left->storage < right->storage;
            }
            if (left->stableKey != right->stableKey)
            {
                return left->stableKey < right->stableKey;
            }
            return left->stableName < right->stableName;
        });

        for (std::size_t index = 0; index < ordered.size(); ++index)
        {
            ComponentInfo* info = ordered[index];
            if (info->stableName.empty())
            {
                throw std::invalid_argument("ECS component stable name cannot be empty");
            }
            if (index > 0 && ordered[index - 1]->stableKey == info->stableKey)
            {
                if (ordered[index - 1]->stableName == info->stableName)
                {
                    throw std::logic_error("Duplicate ECS component stable key");
                }
                throw std::logic_error("ECS component stable-key hash collision");
            }
        }

        if (ordered.size() >= static_cast<std::size_t>(InvalidComponentId))
        {
            throw std::length_error("Too many ECS components for dense ComponentId");
        }

        m_idToInfo = ordered;
        std::vector<ComponentInfo*> tables;
        for (ComponentInfo* info : ordered)
        {
            if (info->storage == ComponentStorage::Table)
            {
                tables.push_back(info);
            }
        }
        m_schemaHash = ComputeSchemaHash(tables);

        for (ComponentId id = 0; id < ordered.size(); ++id)
        {
            ordered[id]->id = id;
        }

        std::size_t capacity = 16;
        while (capacity < ordered.size() * 2)
            capacity *= 2;
        m_keySlots.assign(capacity, KeySlot{});
        m_keyMask = capacity - 1;
        for (const ComponentInfo* info : ordered)
        {
            std::size_t slot = static_cast<std::size_t>(info->stableKey ^ (info->stableKey >> 29)) & m_keyMask;
            while (m_keySlots[slot].id != InvalidComponentId)
                slot = (slot + 1) & m_keyMask;
            m_keySlots[slot] = KeySlot{ info->stableKey, info->id };
        }

        m_frozen = true;
    }
}
