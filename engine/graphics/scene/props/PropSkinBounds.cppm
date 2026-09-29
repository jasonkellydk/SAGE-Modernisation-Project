export module Graphics.Scene.Props.SkinBounds;
import std;
import Graphics.Scene.Props.Geometry;
import Graphics.Scene.Props.SkinPalettes;

namespace Graphics {
// Cache bind-pose bounds by bone once. Evaluating a pose visits bones, not
// every vertex, and conservatively includes every deformed mesh position.
export class PropSkinBounds final {
public:
    void Clear() noexcept { m_bones.clear(); m_last_pose.clear(); m_prepared=false; m_cached=false; }
    bool Evaluate(const PropGeometry& geometry,std::span<const PropBoneTransform> pose,
        std::array<float,3>& minimum,std::array<float,3>& maximum)
    {
        if (pose.empty()) {
            minimum=geometry.Minimum_Position(); maximum=geometry.Maximum_Position();
            return true;
        }
        if (geometry.Maximum_Bone_Index()>=pose.size()) return false;
        if (!m_prepared) {
            m_bones.resize(geometry.Maximum_Bone_Index()+1);
            for (const auto& vertex : geometry.Vertices()) {
                const auto index=static_cast<std::uint32_t>(vertex.bone_index);
                auto& bone=m_bones[index];
                if (!bone.used) {
                    bone.minimum=bone.maximum=vertex.position; bone.index=index; bone.used=true;
                } else for (unsigned axis=0; axis<3; ++axis) {
                    bone.minimum[axis]=(std::min)(bone.minimum[axis],vertex.position[axis]);
                    bone.maximum[axis]=(std::max)(bone.maximum[axis],vertex.position[axis]);
                }
            }
            std::erase_if(m_bones,[](const Bone& bone) { return !bone.used; });
            m_last_pose.resize(m_bones.size());
            m_prepared=true;
        }
        if (m_cached) {
            bool same=true;
            for (std::size_t index=0;index<m_bones.size();++index)
                if (std::memcmp(&m_last_pose[index],&pose[m_bones[index].index],sizeof(PropBoneTransform))!=0) {
                    same=false; break;
                }
            if (same) { minimum=m_minimum; maximum=m_maximum; return true; }
        }
        minimum.fill((std::numeric_limits<float>::max)());
        maximum.fill(std::numeric_limits<float>::lowest());
        for (const auto& bone : m_bones) {
            const auto& matrix=pose[bone.index];
            for (unsigned axis=0; axis<3; ++axis) {
                const unsigned row=axis*4;
                // Each affine row reaches its extrema at independently chosen
                // box endpoints. Evaluate the interval once instead of all eight
                // corners, retaining outward rounding for GPU float arithmetic.
                double lower=matrix[row+3], upper=lower, magnitude=std::abs(lower);
                for (unsigned column=0; column<3; ++column) {
                    const double a=double(matrix[row+column])*bone.minimum[column];
                    const double b=double(matrix[row+column])*bone.maximum[column];
                    lower+=(std::min)(a,b);
                    upper+=(std::max)(a,b);
                    magnitude+=(std::max)(std::abs(a),std::abs(b));
                }
                const double error=8*std::numeric_limits<float>::epsilon()*magnitude
                    +8*std::numeric_limits<float>::min();
                const float low=std::nextafter(static_cast<float>(lower-error),-std::numeric_limits<float>::infinity());
                const float high=std::nextafter(static_cast<float>(upper+error),std::numeric_limits<float>::infinity());
                minimum[axis]=(std::min)(minimum[axis],low);
                maximum[axis]=(std::max)(maximum[axis],high);
            }
        }
        if (m_bones.empty()) minimum=maximum={};
        for (std::size_t index=0;index<m_bones.size();++index)
            m_last_pose[index]=pose[m_bones[index].index];
        m_minimum=minimum; m_maximum=maximum; m_cached=true;
        return true;
    }
private:
    struct Bone {
        std::array<float,3> minimum{},maximum{};
        std::uint32_t index=0;
        bool used=false;
    };
    std::vector<Bone> m_bones;
    std::vector<PropBoneTransform> m_last_pose;
    std::array<float,3> m_minimum{}, m_maximum{};
    bool m_cached=false;
    bool m_prepared=false;
};
}
