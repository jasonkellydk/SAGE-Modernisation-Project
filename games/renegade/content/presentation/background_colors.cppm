export module games.renegade.content.presentation.background_colors;
import std;
export import games.renegade.content.levels.background;
import Engine.Core.Math.FixedPresentation;
export namespace renegade::content {
struct SkyAppearance {std::array<float,4> sky{},horizon{};};
inline SkyAppearance BackgroundColors(const BackgroundDefinition& definition) {
    // SkyClass::Set_Color/Set_Time_Of_Day (EA backgroundmgr.cpp): hourly
    // colors, logarithmic gloominess and red tint are game content.
    constexpr std::array<std::array<unsigned,3>,24> sky{{{4,12,18},{4,12,18},{4,12,18},{4,12,18},{8,24,40},{72,92,136},{120,120,128},{128,152,168},{112,148,168},{112,148,176},{112,148,176},{112,148,176},{112,148,176},{112,148,176},{112,148,176},{112,146,176},{128,160,176},{120,148,152},{160,132,112},{72,88,88},{8,24,36},{4,12,18},{4,12,18},{4,12,18}}};
    constexpr std::array<std::array<unsigned,3>,24> horizon{{{36,36,40},{36,36,40},{36,36,40},{36,36,40},{51,83,100},{170,164,205},{200,179,182},{215,215,181},{215,215,209},{215,215,209},{215,215,209},{215,215,209},{215,215,209},{215,215,209},{215,215,209},{215,215,209},{210,209,176},{213,190,135},{214,187,123},{179,123,98},{72,72,80},{36,36,40},{36,36,40},{36,36,40}}};
    const auto hour=definition.hours%24,next=(hour+1)%24;const auto fraction=definition.minutes/60.f;
    const auto gloom=std::log10(Engine::Math::ToFloat(definition.gloominess)*9+1),tint=Engine::Math::ToFloat(definition.tint);
    SkyAppearance result;result.sky[3]=result.horizon[3]=1;
    for(unsigned channel=0;channel<3;++channel) {
        const auto ordinary=std::lerp(float(sky[hour][channel]),float(sky[next][channel]),fraction)/255;
        const auto grey=[&](unsigned h,unsigned c) {return h>=6 && h<=18 ? 192.f : float(sky[h][c]);};
        const auto overcast=std::lerp(grey(hour,channel),grey(next,channel),fraction)/255;
        result.sky[channel]=std::lerp(std::lerp(ordinary,overcast,gloom),channel==0 ? 1.f : 0.f,tint);
        result.horizon[channel]=std::lerp(float(horizon[hour][channel]),float(horizon[next][channel]),fraction)/255;
    }
    return result;
}
}
