export module games.renegade.presentation.menu.movie_gallery;
import std;
import engine.config.adapters.preferences.preferences_file;
import engine.gui.w3d.list_selection;
import engine.gui.mvvm.observable;

export namespace renegade::presentation {
struct GalleryMovie {std::string filename,description;};
inline constexpr std::string_view unlocked_movie_prefix="unlocked movie ";
// The legacy registry subkey becomes portable preferences. Campaign composition
// records only movies actually reached; a new profile has only the intro.
std::optional<std::string> GalleryMoviePath(std::string_view input) {
    if(input.empty() || input.size()>1024) return {};
    std::string path(input);
    for(char& ch:path) {if(ch=='\\') ch='/';if(ch>='A' && ch<='Z') ch+='a'-'A';}
    if(!path.starts_with("data/movies/") || !path.ends_with(".bik") || path.find_first_of("\r\n=;:")!=path.npos) return {};
    for(const auto& part:std::filesystem::path(path)) if(part==".." || part==".") return {};
    return path;
}
bool UnlockGalleryMovie(engine::config::Preferences& preferences,std::string_view filename,std::string_view description) {
    const auto path=GalleryMoviePath(filename);
    if(!path || description.empty() || description.size()>256 || description.find_first_of("\r\n")!=description.npos) return false;
    preferences.Set(std::string(unlocked_movie_prefix)+*path,description);return true;
}
std::vector<GalleryMovie> ReadGalleryMovies(const engine::config::Preferences& preferences) {
    // MovieOptions::On_Init_Dialog inserts the intro first, then registry entries.
    std::vector<GalleryMovie> movies{{"data/movies/r_intro.bik","IDS_INTRO_MOVIE"}};
    for(const auto& [key,value]:preferences.Entries()) if(key.starts_with(unlocked_movie_prefix))
        if(auto path=GalleryMoviePath(std::string_view(key).substr(unlocked_movie_prefix.size())))
            movies.push_back({std::move(*path),value});
    return movies;
}
class MovieGallery final {
public:
    engine::gui::w3d::ListSelectionModel list;
    engine::gui::mvvm::Observable<bool> playing{false};
    engine::gui::mvvm::Command play;
    MovieGallery() {play.SetAction([this]{Begin();});}
    bool Open(std::vector<GalleryMovie> movies) {
        if(movies.size()>65536 || !list.Configure(std::vector<float>(movies.size(),1),1)) return false;
        m_movies=std::move(movies);m_requested.reset();playing.Set(false);
        return true;
    }
    std::span<const GalleryMovie> Movies() const noexcept {return m_movies;}
    bool Begin() {
        if(playing.Get() || list.selected.Get()<0 || static_cast<std::size_t>(list.selected.Get())>=m_movies.size()) return false;
        m_requested=m_movies[list.selected.Get()].filename;playing.Set(true);return true;
    }
    std::optional<std::string> TakeRequest() {return std::exchange(m_requested,{});}
    void Complete() {m_requested.reset();playing.Set(false);}
    // MovieOptions::On_Key_Down consumes Escape to stop the movie first. Other
    // keys and commands are ignored during playback; Escape later returns.
    bool Escape() {if(!playing.Get()) return false;Complete();return true;}
private:
    std::vector<GalleryMovie> m_movies;
    std::optional<std::string> m_requested;
};
}
