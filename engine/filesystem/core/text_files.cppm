export module engine.filesystem.core.text_files;
import std;

export namespace engine::filesystem {
std::expected<bool,std::error_code> PathExists(const std::filesystem::path& path) {
    std::error_code error;const auto exists=std::filesystem::exists(path,error);
    return error ? std::expected<bool,std::error_code>(std::unexpected(error)) : std::expected<bool,std::error_code>(exists);
}
std::expected<bool,std::error_code> RemoveRegularFile(const std::filesystem::path& path) {
    std::error_code error;const auto status=std::filesystem::status(path,error);
    if(error==std::errc::no_such_file_or_directory) return false;
    if(error) return std::unexpected(error);
    if(!std::filesystem::exists(status)) return false;
    if(!std::filesystem::is_regular_file(status)) return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    const auto removed=std::filesystem::remove(path,error);
    return error ? std::expected<bool,std::error_code>(std::unexpected(error)) : std::expected<bool,std::error_code>(removed);
}
// Writable user files are separate from the read-only mounted asset namespace.
// Callers own locations and schemas. A failed replacement preserves the prior
// file; renaming a sibling keeps the operation on the same filesystem.
std::expected<std::string,std::error_code> ReadTextFile(const std::filesystem::path& path,
    std::size_t maximum_bytes=8*1024*1024) {
    std::error_code error;
    const auto status=std::filesystem::status(path,error);
    if(error) return std::unexpected(error);
    if(!std::filesystem::is_regular_file(status)) return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    std::ifstream input(path,std::ios::binary|std::ios::ate);
    if(!input) return std::unexpected(std::make_error_code(std::errc::io_error));
    const auto size=input.tellg();
    if(size<0) return std::unexpected(std::make_error_code(std::errc::io_error));
    if(static_cast<std::uint64_t>(size)>maximum_bytes) return std::unexpected(std::make_error_code(std::errc::file_too_large));
    std::string text(static_cast<std::size_t>(size),'\0');input.seekg(0);
    if(!input.read(text.data(),static_cast<std::streamsize>(text.size())))
        return std::unexpected(std::make_error_code(std::errc::io_error));
    return text;
}

std::expected<void,std::error_code> WriteTextFile(const std::filesystem::path& path,std::string_view text) {
    if(path.filename().empty() || text.size()>static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    std::error_code error;
    if(!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(),error);
    if(error) return std::unexpected(error);
    static std::atomic<std::uint64_t> sequence{};
    auto temporary=path;
    temporary += ".tmp-"+std::to_string(std::chrono::steady_clock::now().time_since_epoch().count())+
        "-"+std::to_string(sequence.fetch_add(1,std::memory_order_relaxed));
    std::ofstream output(temporary,std::ios::binary|std::ios::out|std::ios::noreplace);
    if(!output) return std::unexpected(std::make_error_code(std::errc::io_error));
    struct RemoveTemporary {
        std::filesystem::path path;
        ~RemoveTemporary() {std::error_code ignored;std::filesystem::remove(path,ignored);}
    } cleanup{temporary};
    output.write(text.data(),static_cast<std::streamsize>(text.size()));output.close();
    if(!output) return std::unexpected(std::make_error_code(std::errc::io_error));
    std::filesystem::rename(temporary,path,error);
    if(error) return std::unexpected(error);
    return {};
}
}
