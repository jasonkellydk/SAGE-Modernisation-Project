export module engine.ecs.core.state_hash;
import std;

import Engine.Core.Math.RandomStream;

export namespace ecs
{
using StateHashValue = std::uint64_t;

// Order-sensitive 64-bit hash of simulation state, used to detect lockstep
// desyncs and to check determinism in tests. Bytes are consumed in 8-byte
// little-endian words; every supported target is little-endian.
class StateHasher
{
public:
	void AppendU64(std::uint64_t value) noexcept
	{
		m_hash = Engine::Math::RandomStream::Derive_Seed(m_hash, value);
	}

	void AppendBytes(std::span<const std::byte> bytes) noexcept
	{
		AppendU64(bytes.size());
		std::size_t offset = 0;
		for (; offset + 8 <= bytes.size(); offset += 8)
		{
			std::uint64_t word;
			std::memcpy(&word, bytes.data() + offset, 8);
			AppendU64(word);
		}
		if (offset < bytes.size())
		{
			std::uint64_t word = 0;
			std::memcpy(&word, bytes.data() + offset, bytes.size() - offset);
			AppendU64(word);
		}
	}

	template<typename T>
	void AppendValue(const T &value) noexcept
	{
		AppendBytes(std::as_bytes(std::span{&value, 1}));
	}

	StateHashValue Value() const noexcept { return m_hash; }

private:
	StateHashValue m_hash{0x243F6A8885A308D3u};
};
}
