export module Video.Adapters.MemorySource;
import std;
import Video.Decoder;
export namespace Engine::Video {
// Owns decoded container bytes supplied by any filesystem/archive adapter.
// FFmpeg and playback remain independent of physical paths and game schemas.
class MemorySource final : public Source {
public:
    explicit MemorySource(std::vector<std::byte> bytes):m_bytes(std::move(bytes)) {
        if(m_bytes.size()>static_cast<std::size_t>(std::numeric_limits<std::int64_t>::max()))
            throw std::length_error("video source exceeds signed seek capacity");
    }
    std::size_t Read(std::span<std::byte> destination) noexcept override {
        const auto count=std::min(destination.size(),m_bytes.size()-static_cast<std::size_t>(m_position));
        std::copy_n(m_bytes.begin()+m_position,count,destination.begin());m_position+=static_cast<std::int64_t>(count);return count;
    }
    bool Seek(std::int64_t offset,SourceSeekOrigin origin) noexcept override {
        std::int64_t base{};
        switch(origin) {case SourceSeekOrigin::Begin:break;case SourceSeekOrigin::Current:base=m_position;break;
            case SourceSeekOrigin::End:base=Size();break;default:return false;}
        if(offset< -base || offset>Size()-base) return false;
        m_position=base+offset;return true;
    }
    std::int64_t Tell() const noexcept override {return m_position;}
    std::int64_t Size() const noexcept override {return static_cast<std::int64_t>(m_bytes.size());}
private:
    std::vector<std::byte> m_bytes;
    std::int64_t m_position{};
};
}
