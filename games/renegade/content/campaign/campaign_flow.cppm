export module games.renegade.content.campaign.campaign_flow;
import std;
import engine.config.adapters.ini.section_reader;
import engine.filesystem.core.virtual_file_system;

export namespace renegade::content {
enum class CampaignStepKind { Level, Movie, Score, Message };
struct CampaignStep {
    CampaignStepKind kind{};
    std::string asset,description;
};
struct CampaignFlow {std::vector<CampaignStep> steps;};

// Commando/campaign.cpp Init/Continue enumerate values in declaration order.
// Numeric keys are labels, not indices, and commented entries never execute.
inline std::expected<CampaignFlow,std::string> ReadCampaignFlow(std::string text) {
    const auto document=engine::config::ini::ReadSections("campaign.ini",std::move(text));
    if(!document) return std::unexpected(document.error());
    const auto* section=engine::config::ini::FindSection(*document,"Campaign");
    if(!section || section->children.empty()) return std::unexpected("campaign has no flow entries");
    const auto trim=[](std::string_view value) {
        while(!value.empty() && static_cast<unsigned char>(value.front())<=' ') value.remove_prefix(1);
        while(!value.empty() && static_cast<unsigned char>(value.back())<=' ') value.remove_suffix(1);
        return value;
    };
    CampaignFlow flow;
    for(const auto& entry:section->children) {
        auto value=trim(entry.Value());CampaignStep step;
        if(value.starts_with("Level ")) {step.kind=CampaignStepKind::Level;step.asset=trim(value.substr(6));}
        else if(value.starts_with("Movie ")) {
            step.kind=CampaignStepKind::Movie;value=trim(value.substr(6));
            const auto space=value.find_first_of(" \t");
            if(space==value.npos) return std::unexpected("campaign movie lacks a description");
            step.asset=value.substr(0,space);step.description=trim(value.substr(space+1));
            if(step.description.empty()) return std::unexpected("campaign movie lacks a description");
        } else if(value.starts_with("Score")) {step.kind=CampaignStepKind::Score;step.description=trim(value.substr(5));}
        else if(value.starts_with("Message ")) {step.kind=CampaignStepKind::Message;step.description=trim(value.substr(8));}
        else return std::unexpected("unsupported campaign entry: "+std::string(value));
        if(step.kind==CampaignStepKind::Level || step.kind==CampaignStepKind::Movie) {
            for(char& c:step.asset) if(c=='\\') c='/';
            const auto path=std::filesystem::path(step.asset);
            if(step.asset.empty() || path.is_absolute() || step.asset.find(':')!=step.asset.npos ||
                std::ranges::any_of(path,[](const auto& part){return part==".." || part==".";}))
                return std::unexpected("invalid campaign asset path");
        }
        flow.steps.push_back(std::move(step));
    }
    return flow;
}
inline std::expected<CampaignFlow,std::string> LoadCampaignFlow(const engine::filesystem::VirtualFileSystem& files) {
    const auto text=files.ReadText("campaign.ini");
    if(!text) return std::unexpected("campaign.ini missing");
    return ReadCampaignFlow(*text);
}
}
