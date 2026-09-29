export module Graphics.Scene.Models.AnimationRotation;
import std;

namespace Graphics {
namespace RotationDetail {
constexpr unsigned TableSize=1024;
constexpr float Pi=3.141592654f;
struct Tables {
    std::array<float,TableSize> arc,sine;
    Tables() {
        for(unsigned i=0;i<TableSize;++i) {
            const float cosine=float(int(i)-int(TableSize/2))*(1.0f/(TableSize/2));
            arc[i]=static_cast<float>(std::acos(static_cast<double>(cosine)));
            const float angle=float(i)*2.0f*Pi/TableSize;
            sine[i]=static_cast<float>(std::sin(static_cast<double>(angle)));
        }
    }
};
const Tables& Lookup() { static const Tables tables;return tables; }
float Sine(float angle) {
    angle*=float(TableSize)/(2.0f*Pi);
    const auto first=static_cast<int>(std::floor(angle));
    const float fraction=angle-float(first);
    const auto& values=Lookup().sine;
    return (1.0f-fraction)*values[unsigned(first)&(TableSize-1)]
        +fraction*values[unsigned(first+1)&(TableSize-1)];
}
float ArcCosine(float cosine) {
    if(std::fabs(cosine)>.975f)return static_cast<float>(std::acos(static_cast<double>(cosine)));
    cosine*=float(TableSize/2);
    const auto first=static_cast<int>(std::floor(cosine));
    const float fraction=cosine-float(first);
    const auto& values=Lookup().arc;
    return (1.0f-fraction)*values[first+TableSize/2]+fraction*values[first+1+TableSize/2];
}
}

// Preserve authored magnitudes and sampled approximations; normalization is
// not part of this interpolation contract. Extrapolation is used by pose clips.
export class PreparedAnimationRotation final {
public:
    PreparedAnimationRotation()=default;
    PreparedAnimationRotation(const std::array<float,4>& first,const std::array<float,4>& second)
        : m_first(first),m_second(second) {
        float cosine=first[0]*second[0]+first[1]*second[1]+first[2]*second[2]+first[3]*second[3];
        m_flip=cosine<0;
        if (m_flip) cosine=-cosine;
        m_linear=1.0f-cosine<.0001f*.0001f;
        if (!m_linear) {
            m_angle=RotationDetail::ArcCosine(cosine);
            m_inverse_sine=1.0f/RotationDetail::Sine(m_angle);
        }
    }
    std::array<float,4> Sample(float weight) const {
        float complement;
        if (m_linear) complement=1.0f-weight;
        else {
            complement=RotationDetail::Sine(m_angle-weight*m_angle)*m_inverse_sine;
            weight=RotationDetail::Sine(weight*m_angle)*m_inverse_sine;
        }
        if (m_flip) weight=-weight;
        std::array<float,4> result;
        for (unsigned i=0;i<4;++i) result[i]=complement*m_first[i]+weight*m_second[i];
        return result;
    }
    const std::array<float,4>& First() const noexcept { return m_first; }
    const std::array<float,4>& Second() const noexcept { return m_second; }
private:
    std::array<float,4> m_first{0,0,0,1},m_second{0,0,0,1};
    float m_angle=0,m_inverse_sine=1;
    bool m_flip=false,m_linear=true;
};

export std::array<float,4> Interpolate_Animation_Rotation(
    const std::array<float,4>& first,const std::array<float,4>& second,float weight) {
    return PreparedAnimationRotation(first,second).Sample(weight);
}

// Application adapters translate row-addressable quaternion values without
// introducing a dependency on the type's library or retaining duplicate state.
export template<class Quaternion>
void Interpolate_Animation_Rotation(Quaternion& result,const Quaternion& first,const Quaternion& second,float weight) {
    const auto values=Interpolate_Animation_Rotation(
        {first[0],first[1],first[2],first[3]},{second[0],second[1],second[2],second[3]},weight);
    for(unsigned i=0;i<4;++i)result[i]=values[i];
}
}
