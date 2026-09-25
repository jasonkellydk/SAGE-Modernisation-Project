cmake_minimum_required(VERSION 3.28)

add_library(engine_core_math STATIC)
target_sources(engine_core_math PUBLIC
    FILE_SET CXX_MODULES
    BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES
        scalar/Scalar.cppm
        scalar/TurnAngle.cppm
        random/RandomStream.cppm
        random/RandomVector3Generator.cppm
        spatial/SpatialGrid3.cppm
        integer/Index3.cppm
        geometry/AxisAlignedBox3.cppm
        geometry/BoundsQueries3.cppm
        geometry/CollisionResult3.cppm
        geometry/LineGeometry3.cppm
        geometry/LineSegment3.cppm
        geometry/OrientedBox3.cppm
        geometry/Plane3.cppm
        geometry/PolygonClip3.cppm
        geometry/Rectangle2.cppm
        geometry/Sphere3.cppm
        geometry/Triangle3.cppm
        transform/AffineTransform3.cppm
        transform/EulerAngles3.cppm
        transform/Matrix3.cppm
        transform/Matrix4.cppm
        transform/Quaternion.cppm
        transform/QuaternionInterpolator.cppm
        vector/Vector2.cppm
        vector/Vector3.cppm
        vector/Vector4.cppm)
target_compile_features(engine_core_math PUBLIC cxx_std_23)

# MSVC's default /fp:precise already forbids contraction; do not force
# /fp:strict on every consumer of this library.
if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
	target_compile_options(engine_core_math PUBLIC /clang:-ffp-contract=off)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
	target_compile_options(engine_core_math PUBLIC -ffp-contract=off)
endif()

if(BUILD_TESTING)
    find_package(Boost REQUIRED)
    add_executable(engine_core_math_test)
    target_sources(engine_core_math_test PRIVATE
        FILE_SET CXX_MODULES
        BASE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}"
    FILES tests/Math.test.cppm)
    target_include_directories(engine_core_math_test PRIVATE ${Boost_INCLUDE_DIRS})
    target_link_libraries(engine_core_math_test PRIVATE engine_core_math)
    add_test(NAME engine_core_math_test COMMAND engine_core_math_test)
endif()
