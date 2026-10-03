export module games.renegade.content.presentation.loading_screen;
import std;
import engine.config.adapters.ini.section_reader;
import engine.filesystem.core.virtual_file_system;

export namespace renegade::content
{
struct LoadingScreenText
{
	float x{},y{},wrap{};
	std::uint32_t color{0xffffffff};
	bool large{},literal{};
	std::string text;
};
struct LoadingScreen
{
	int number{};
	std::string model;
	std::vector<LoadingScreenText> texts;
};
inline int LoadingScreenNumber(std::string_view map)
{
	const auto name=engine::config::ini::FoldAscii(std::filesystem::path(map).filename().string());
	// cGameData::Get_Mission_Number_From_Map_Name; the tutorial explicitly
	// selects 90, while M13 selects the campaign prologue's Backdrop13.
	if(name.starts_with("m00_t")) return 90;
	int number{};
	if(name.size()>1) std::from_chars(name.data()+1,name.data()+name.size(),number);
	return number;
}
inline std::expected<LoadingScreen,std::string> ReadLoadingScreen(std::string text,int number)
{
	const auto document=engine::config::ini::ReadSections("campaign.ini",std::move(text));
	if(!document) return std::unexpected(document.error());
	const auto* section=engine::config::ini::FindSection(*document,"Backdrop"+std::to_string(number));
	if(section && section->children.empty()) section=nullptr;
	if(!section) for(int fallback=0;fallback<100;++fallback) {
		const auto* candidate=engine::config::ini::FindSection(*document,"Backdrop"+std::to_string(fallback));
		if(candidate && !candidate->children.empty()) {section=candidate;number=fallback;break;}
	}
	if(!section) return std::unexpected("campaign has no loading backdrop");
	LoadingScreen result;result.number=number;
	std::array<float,2> wrap{};std::uint32_t color=0xffffffff;
	const auto trim=[](std::string_view value) {
		while(!value.empty() && static_cast<unsigned char>(value.front())<=' ') value.remove_prefix(1);
		while(!value.empty() && static_cast<unsigned char>(value.back())<=' ') value.remove_suffix(1);
		return value;
	};
	const auto scalar=[&](std::string_view value)->std::optional<float> {
		value=trim(value);float parsed{};const auto read=std::from_chars(value.data(),value.data()+value.size(),parsed);
		if(read.ec!=std::errc{} || read.ptr!=value.data()+value.size() || !std::isfinite(parsed)) return {};
		return parsed;
	};
	for(const auto& entry:section->children) {
		const auto value=trim(entry.Value());const auto space=value.find_first_of(" \t");
		const auto command=engine::config::ini::FoldAscii(value.substr(0,space));
		auto arguments=space==std::string_view::npos ? std::string_view{} : trim(value.substr(space+1));
		if(command=="model") {if(arguments.empty()) return std::unexpected("loading backdrop model missing");result.model=arguments;}
		else if(command=="color") {
			std::array<unsigned,3> channels{};
			for(unsigned i=0;i<3;++i) {
				const auto comma=arguments.find(',');const auto field=scalar(arguments.substr(0,comma));
				if(!field || *field<0 || *field>255 || (i<2 && comma==std::string_view::npos) || (i==2 && comma!=std::string_view::npos))
					return std::unexpected("invalid loading text color");
				channels[i]=static_cast<unsigned>(*field);if(i<2) arguments.remove_prefix(comma+1);
			}
			color=0xff000000u | channels[0]<<16 | channels[1]<<8 | channels[2];
		} else if(command=="wrap" || command=="wrap2") {
			const auto width=scalar(arguments);if(!width || *width<0) return std::unexpected("invalid loading text wrap");
			wrap[command=="wrap2"]=*width;
		} else if(command=="text" || command=="text2" || command=="test") {
			LoadingScreenText line;line.large=command=="text2";line.literal=command=="test";line.color=color;line.wrap=wrap[line.large];
			const auto first=arguments.find(',');if(first==std::string_view::npos) return std::unexpected("loading text lacks x");
			const auto x=scalar(arguments.substr(0,first));arguments.remove_prefix(first+1);
			const auto second=arguments.find(',');if(second==std::string_view::npos) return std::unexpected("loading text lacks y");
			const auto y=scalar(arguments.substr(0,second));arguments.remove_prefix(second+1);
			if(!x || !y || trim(arguments).empty()) return std::unexpected("invalid loading text placement");
			line.x=*x;line.y=*y;line.text=trim(arguments);result.texts.push_back(std::move(line));
			color=0xffffffff;wrap[command=="text2"]=0;
		}
	}
	if(result.model.empty()) return std::unexpected("loading backdrop has no model");
	return result;
}
inline std::expected<LoadingScreen,std::string> LoadLoadingScreen(const engine::filesystem::VirtualFileSystem& files,std::string_view map)
{
	const auto text=files.ReadText("campaign.ini");if(!text) return std::unexpected("campaign.ini missing");
	return ReadLoadingScreen(*text,LoadingScreenNumber(map));
}
// LoadingScreenClass preserves the authored beam motion but modern loading
// supplies measured task progress instead of the old predicted 18-second load.
inline float AdvanceLoadingAnimation(float drawn,float target)
{
	return drawn+(std::clamp(target,0.f,1.f)-drawn)*0.1f;
}
}
