export module Engine.Core.Math.FixedAffineTransform3;
import std;
export import Engine.Core.Math.FixedVector;

export namespace Engine::Math
{
// Row-major 3x4 affine basis for simulation objects with full authored poses.
// It retains scale/roll/pitch, using the same Q48.16 scalar as world positions.
struct FixedAffineTransform3
{
	std::array<Fixed, 12> elements{Fixed::One(), {}, {}, {}, {}, Fixed::One(), {}, {}, {}, {}, Fixed::One(), {}};
	constexpr FixedVector3 Point(FixedVector3 point) const noexcept {
		return {elements[0]*point.x+elements[1]*point.y+elements[2]*point.z+elements[3],
			elements[4]*point.x+elements[5]*point.y+elements[6]*point.z+elements[7],
			elements[8]*point.x+elements[9]*point.y+elements[10]*point.z+elements[11]};
	}
	constexpr FixedAffineTransform3 operator*(const FixedAffineTransform3& right) const noexcept {
		FixedAffineTransform3 result;
		for(unsigned row=0;row<3;++row) {
			for(unsigned column=0;column<3;++column) {
				result.elements[row*4+column]={};
				for(unsigned k=0;k<3;++k) result.elements[row*4+column]+=elements[row*4+k]*right.elements[k*4+column];
			}
			result.elements[row*4+3]=elements[row*4+3];
			for(unsigned k=0;k<3;++k) result.elements[row*4+3]+=elements[row*4+k]*right.elements[k*4+3];
		}
		return result;
	}
	static constexpr FixedAffineTransform3 From_Translation(FixedVector3 value) noexcept {
		FixedAffineTransform3 result;
		result.elements[3] = value.x; result.elements[7] = value.y; result.elements[11] = value.z;
		return result;
	}
};
}
