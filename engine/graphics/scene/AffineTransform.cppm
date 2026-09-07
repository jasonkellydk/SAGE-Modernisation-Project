module;
#include <array>
#include <cstddef>
export module Graphics.Scene.AffineTransform;
export import Graphics.Scene.RenderScene;

namespace Graphics {
export RenderTransform Affine_Identity() noexcept {
    return {{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}};
}

// Row-addressable application matrices need no dependency on their declaring
// library here. Only affine rows cross this boundary; the last row is explicit.
export template<class Matrix>
RenderTransform Import_Affine_Transform(const Matrix& source) noexcept {
    auto result=Affine_Identity();
    for(unsigned row=0;row<3;++row)
        for(unsigned column=0;column<4;++column)
            result.matrix[row*4+column]=source[row][column];
    return result;
}

export template<class Matrix>
Matrix Export_Affine_Transform(const RenderTransform& source) noexcept {
    Matrix result;
    for(unsigned row=0;row<3;++row)
        for(unsigned column=0;column<4;++column)
            result[row][column]=source.matrix[row*4+column];
    return result;
}

export RenderTransform Multiply_Affine(const RenderTransform& left,const RenderTransform& right) noexcept {
    auto result=Affine_Identity();
    const auto& a=left.matrix;const auto& b=right.matrix;
    for(unsigned row=0;row<3;++row) {
        const auto base=row*4;
        for(unsigned column=0;column<3;++column)
            result.matrix[base+column]=a[base]*b[column]+a[base+1]*b[4+column]+a[base+2]*b[8+column];
        result.matrix[base+3]=a[base]*b[3]+a[base+1]*b[7]+a[base+2]*b[11]+a[base+3];
    }
    return result;
}

export void Translate_Affine(RenderTransform& transform,const std::array<float,3>& translation) noexcept {
    auto& m=transform.matrix;
    for(unsigned row=0;row<3;++row) {
        const auto base=row*4;
        m[base+3]+=m[base]*translation[0]+m[base+1]*translation[1]+m[base+2]*translation[2];
    }
}

// Sampling owns quaternion normalization/interpolation. Do not renormalize
// here: authored values and sampled approximations must retain their matrix.
export RenderTransform Quaternion_Affine(const std::array<float,4>& q) noexcept {
    auto result=Affine_Identity();auto& m=result.matrix;
    m[0]=float(1.0-2.0*(q[1]*q[1]+q[2]*q[2]));
    m[1]=float(2.0*(q[0]*q[1]-q[2]*q[3]));
    m[2]=float(2.0*(q[2]*q[0]+q[1]*q[3]));
    m[4]=float(2.0*(q[0]*q[1]+q[2]*q[3]));
    m[5]=float(1.0-2.0f*(q[2]*q[2]+q[0]*q[0]));
    m[6]=float(2.0*(q[1]*q[2]-q[0]*q[3]));
    m[8]=float(2.0*(q[2]*q[0]-q[1]*q[3]));
    m[9]=float(2.0*(q[1]*q[2]+q[0]*q[3]));
    m[10]=float(1.0-2.0*(q[1]*q[1]+q[0]*q[0]));
    return result;
}
}
