export module games.renegade.content.presentation.input_profile_store;
import std;
export import games.renegade.content.input.profile_catalog;
export import games.renegade.content.presentation.input_configuration;
import engine.filesystem.core.text_files;

export namespace renegade::content {
class InputProfileStore final {
public:
    using AssetReader=std::function<std::optional<std::string>(std::string_view)>;
    InputProfileStore(std::filesystem::path directory,AssetReader read):m_directory(std::move(directory)),m_read(std::move(read)) {}
    const InputProfileCatalog& Catalog() const {return m_catalog;}
    std::expected<void,std::string> Open(std::u16string default_name,std::u16string custom_prefix) {
        m_custom_prefix=std::move(custom_prefix);
        const auto bytes=engine::filesystem::ReadTextFile(m_directory/"profiles.dat");
        if(bytes) {
            auto parsed=ReadInputProfileCatalog(std::as_bytes(std::span(*bytes)));if(!parsed) return std::unexpected(parsed.error());m_catalog=std::move(*parsed);
        } else if(bytes.error()!=std::errc::no_such_file_or_directory) return std::unexpected(bytes.error().message());
        else if(const auto legacy=m_read ? m_read("CONFIG.DAT") : std::nullopt) {
            auto parsed=ReadLegacyInputProfileCatalog(std::as_bytes(std::span(*legacy)));if(!parsed) return std::unexpected(parsed.error());m_catalog=std::move(*parsed);
        }
        const bool initial=m_catalog.profiles.empty();m_catalog.EnsureDefault(std::move(default_name));
        // Adopt the earlier port's single Input.cfg, keeping existing user edits.
        if(initial) {
            const auto old=engine::filesystem::ReadTextFile(m_directory/"Input.cfg",1024*1024);
            if(old) {
                const auto parsed=ReadInputConfiguration(*old);if(!parsed) return std::unexpected(parsed.error());
                m_catalog.profiles.push_back({m_custom_prefix+m_catalog.profiles.front().name,"Input.cfg",false,true});m_catalog.current="Input.cfg";
            } else if(old.error()!=std::errc::no_such_file_or_directory) return std::unexpected(old.error().message());
        }
        return {};
    }
    std::expected<InputConfiguration,std::string> Current() const {return Read(m_catalog.current);}
    std::expected<InputConfiguration,std::string> Read(std::string_view filename) const {
        const auto index=m_catalog.Find(filename);if(!index) return std::unexpected("unknown input profile");
        const auto& profile=m_catalog.profiles[*index];
        if(profile.custom) {
            const auto text=engine::filesystem::ReadTextFile(m_directory/profile.filename,1024*1024);
            if(text) return ReadInputConfiguration(*text);
            if(text.error()!=std::errc::no_such_file_or_directory) return std::unexpected(text.error().message());
            // Legacy CONFIG.DAT entries may refer to custom profiles mounted
            // from retail Data/config. Their first modern save goes to user data.
            const auto legacy=m_read ? m_read(profile.filename) : std::nullopt;
            return legacy ? ReadInputConfiguration(*legacy) : std::expected<InputConfiguration,std::string>(std::unexpected(text.error().message()));
        }
        const auto text=m_read ? m_read(profile.filename) : std::nullopt;
        return text ? ReadInputConfiguration(*text) : std::expected<InputConfiguration,std::string>(std::unexpected("input profile asset missing: "+profile.filename));
    }
    std::expected<InputConfiguration,std::string> Load(std::string_view filename) {
        const auto configuration=Read(filename);if(!configuration) return std::unexpected(configuration.error());
        auto next=m_catalog;next.current=next.profiles[*next.Find(filename)].filename;
        if(auto saved=Commit(std::move(next));!saved) return std::unexpected(saved.error());return *configuration;
    }
    std::expected<void,std::string> Create(std::u16string name,const InputConfiguration& configuration) {
        if(!ValidProfileName(name) || m_catalog.profiles.size()>=4096) return std::unexpected("invalid or too many input profiles");
        std::optional<std::string> filesystem_error;
        const auto filename=UniqueInputProfileFilename(m_catalog,[&](auto candidate) {
            const auto exists=engine::filesystem::PathExists(m_directory/candidate);
            if(!exists) {filesystem_error=exists.error().message();return false;}return *exists;
        });
        if(filesystem_error) return std::unexpected(*filesystem_error);if(!filename) return std::unexpected(filename.error());
        auto next=m_catalog;next.profiles.push_back({std::move(name),*filename,false,true});next.current=*filename;
        const auto profile=next.profiles.back();return SaveAndCommit(profile,configuration,std::move(next));
    }
    std::expected<void,std::string> Save(std::string_view filename,std::u16string name,const InputConfiguration& configuration) {
        const auto index=m_catalog.Find(filename);if(!index || !ValidProfileName(name)) return std::unexpected("invalid input profile save");
        if(!m_catalog.profiles[*index].custom) return Create(m_custom_prefix+m_catalog.profiles[*index].name,configuration);
        auto next=m_catalog;next.profiles[*index].name=std::move(name);next.current=next.profiles[*index].filename;
        const auto profile=next.profiles[*index];return SaveAndCommit(profile,configuration,std::move(next));
    }
    std::expected<void,std::string> SaveCurrent(const InputConfiguration& configuration) {
        const auto index=m_catalog.Find(m_catalog.current);if(!index) return std::unexpected("current input profile missing");
        return Save(m_catalog.current,m_catalog.profiles[*index].name,configuration);
    }
    std::expected<bool,std::string> Delete(std::string_view filename) {
        const auto index=m_catalog.Find(filename);if(!index || !m_catalog.profiles[*index].custom) return false;
        const auto stored=m_catalog.profiles[*index].filename;const bool current=*index==m_catalog.Find(m_catalog.current);
        auto next=m_catalog;next.profiles.erase(next.profiles.begin()+*index);
        if(current) {const auto default_profile=next.Default();if(!default_profile) return std::unexpected("default input profile missing");next.current=next.profiles[*default_profile].filename;}
        const auto path=m_directory/stored;
        // Keep the original bytes, including comments and unknown settings.
        // A legacy mounted custom profile may have no writable local file.
        const auto original=engine::filesystem::ReadTextFile(path,1024*1024);
        if(!original && original.error()!=std::errc::no_such_file_or_directory)
            return std::unexpected(original.error().message());
        const auto removed=engine::filesystem::RemoveRegularFile(path);
        if(!removed) return std::unexpected(removed.error().message());
        if(auto saved=Commit(std::move(next));!saved) {
            const auto failure="input profile catalog update failed: "+saved.error();
            if(!*removed) return std::unexpected(failure);
            if(!original) return std::unexpected(failure+"; removed profile has no rollback copy (path changed during deletion)");
            // This is compensating recovery, not a multi-file transaction.
            // Avoid replacing a path already recreated by another writer.
            // The existence check and restoration still have a race; callers
            // must serialize profile edits to guarantee byte preservation.
            const auto exists=engine::filesystem::PathExists(path);
            if(!exists) return std::unexpected(failure+"; cannot inspect profile restore destination: "+exists.error().message());
            if(*exists) return std::unexpected(failure+"; profile restore blocked because path was recreated");
            if(const auto restored=engine::filesystem::WriteTextFile(path,*original);!restored)
                return std::unexpected(failure+"; profile restore failed: "+restored.error().message());
            return std::unexpected(failure+"; original profile restored");
        }
        return current;
    }
private:
    std::expected<void,std::string> SaveAndCommit(InputProfile profile,const InputConfiguration& configuration,InputProfileCatalog next) {
        if(const auto saved=engine::filesystem::WriteTextFile(m_directory/profile.filename,WriteInputConfiguration(configuration));!saved) return std::unexpected(saved.error().message());
        return Commit(std::move(next));
    }
    std::expected<void,std::string> Commit(InputProfileCatalog next) {
        const auto bytes=WriteInputProfileCatalog(next);if(!bytes) return std::unexpected(bytes.error());
        const std::string_view text(reinterpret_cast<const char*>(bytes->data()),bytes->size());
        if(const auto saved=engine::filesystem::WriteTextFile(m_directory/"profiles.dat",text);!saved) return std::unexpected(saved.error().message());
        m_catalog=std::move(next);return {};
    }
    std::filesystem::path m_directory;AssetReader m_read;InputProfileCatalog m_catalog;std::u16string m_custom_prefix;
};
}
