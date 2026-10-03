export module games.renegade.content.cameras.camera_profiles;
import std;
import engine.config.adapters.ini.section_reader;
export import engine.config.binding.schema;

export namespace renegade::content
{
struct CameraProfile
{
	using Fixed = Engine::Math::Fixed;
	std::string name;
	Fixed fov_degrees{Fixed::FromInt(65)};
	Fixed height{Fixed::FromRatio(195, 100)};
	Fixed view_tilt_degrees{Fixed::FromInt(20)};
	Fixed tilt_tweak{Fixed::FromRatio(6, 10)};
	Fixed translation_tilt_degrees{Fixed::FromRatio(199, 10)};
	Fixed distance{Fixed::FromRatio(31, 10)};
	Fixed lag_up{}, lag_left{}, lag_forward{};
};
struct CameraProfiles
{
	// Names are case folded at the content boundary. Duplicate names replace
	// earlier profiles in Profile_List order, matching ProfileHash.Insert.
	std::map<std::string, CameraProfile> by_name;
	const CameraProfile *Find(std::string_view name) const {
		const auto found = by_name.find(engine::config::ini::FoldAscii(name));
		return found == by_name.end() ? nullptr : &found->second;
	}
};
inline CameraProfile BlendCameraProfile(const CameraProfile& target,const CameraProfile& previous,Engine::Math::Fixed previous_weight) {
    using P=CameraProfile;const auto weight=std::clamp(previous_weight,P::Fixed{},P::Fixed::One());auto result=target;
    constexpr std::array members{&P::fov_degrees,&P::height,&P::view_tilt_degrees,&P::tilt_tweak,
        &P::translation_tilt_degrees,&P::distance,&P::lag_up,&P::lag_left,&P::lag_forward};
    for(const auto member:members) result.*member=target.*member+((previous.*member)-(target.*member))*weight;
    return result;
}

inline engine::config::Schema<CameraProfile> CameraProfileSchema()
{
	using P = CameraProfile;
	engine::config::Schema<P> schema;
	schema.String("name", &P::name).Fixed("fov", &P::fov_degrees)
		.Fixed("height", &P::height).Fixed("viewtilt", &P::view_tilt_degrees)
		.Fixed("tilttweak", &P::tilt_tweak).Fixed("translationtilt", &P::translation_tilt_degrees)
		.Fixed("distance", &P::distance).Fixed("lagup", &P::lag_up)
		.Fixed("lagleft", &P::lag_left).Fixed("lagforward", &P::lag_forward);
	return schema;
}

inline std::expected<CameraProfiles, std::string> ReadCameraProfiles(std::string text)
{
	using namespace engine::config;
	const auto document = ini::ReadSections("cameras.ini", std::move(text));
	if (!document) return std::unexpected(document.error());
	const auto *list = ini::FindSection(*document, "Profile_List");
	if (!list) return std::unexpected("cameras.ini requires Profile_List");
	CameraProfiles profiles; Diagnostics diagnostics; BindContext context{diagnostics, engine::time::FixedStep{60}};
	const auto schema = CameraProfileSchema();
	// CCameraProfileClass::Init enumerates only the listed sections, never
	// every section containing Name. Defaults come from its constructor.
	for (const auto &entry : list->children) {
		const auto *section = ini::FindSection(*document, entry.Value());
		if (!section) return std::unexpected("missing listed camera profile " + std::string(entry.Value()));
		CameraProfile profile; schema.Bind(*section, profile, context);
		if (profile.name.empty()) diagnostics.Error(section->location, "camera profile requires Name");
		if (profile.fov_degrees <= Engine::Math::Fixed{} || profile.fov_degrees >= Engine::Math::Fixed::FromInt(180))
			diagnostics.Error(section->location, "camera FOV must be between zero and 180 degrees");
		profiles.by_name.insert_or_assign(ini::FoldAscii(profile.name), std::move(profile));
	}
	if (diagnostics.HasErrors()) return std::unexpected(diagnostics.Format(*document));
	return profiles;
}
}
