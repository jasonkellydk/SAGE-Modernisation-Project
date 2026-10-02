export module games.renegade.presentation.menu.intro;
import std;
import engine.config.adapters.preferences.preferences_file;

export namespace renegade::presentation
{
struct IntroPreferences { bool skip_allowed{}, skip_all{}; };
// Commando/movie.cpp Init and WWLib/registry.cpp Get_Bool: any nonzero
// integer is true. Keep the original names in the portable preferences file.
IntroPreferences ReadIntroPreferences(const engine::config::Preferences& preferences) {
    return {preferences.Number("IntroMovieSkipAllowed",0)!=0,
        preferences.Number("SkipAllIntroMovies",0)!=0};
}
void WriteIntroPreferences(engine::config::Preferences& preferences,IntroPreferences state) {
    preferences.Set("IntroMovieSkipAllowed",static_cast<std::int64_t>(state.skip_allowed));
    preferences.Set("SkipAllIntroMovies",static_cast<std::int64_t>(state.skip_all));
}
// Commando/movie.cpp Think/Startup_Movies/Movie_Done. EA/Westwood can
// always be skipped. The first Renegade intro must finish before subsequent
// views become skippable. SkipAllIntroMovies also grants that permission.
class IntroSequence {
public:
    explicit IntroSequence(bool enabled=true,bool skip_allowed=false)
        :m_stage(enabled ? 0 : 2),m_skip_allowed(skip_allowed || !enabled) {}
    bool Playing() const { return m_stage<2; }
    std::string_view Movie() const { return m_stage==0 ? "EA_WW.BIK" : m_stage==1 ? "R_Intro.BIK" : ""; }
    bool CanSkip() const { return Playing() && (m_stage==0 || m_skip_allowed); }
    bool SkipAllowed() const { return m_skip_allowed; }
    bool Skip() { if(!CanSkip()) return false;Complete();return true; }
    void Complete() { if(Playing() && ++m_stage==2) m_skip_allowed=true; }
private:
    unsigned m_stage{};
    bool m_skip_allowed{};
};
}
