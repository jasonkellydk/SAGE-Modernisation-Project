module;
#define BOOST_TEST_MODULE ModelAssetPoseTests
#include <boost/test/included/unit_test.hpp>
export module Graphics.Scene.Models.AssetPose.Tests;
import std;
import Graphics.Scene.Models.AssetPose;
import Assets.ModelRig;
using namespace Graphics;
BOOST_AUTO_TEST_CASE(copied_pose_keeps_immutable_source_and_independent_evaluated_bones) {
    Assets::ModelRigDesc rig;rig.skeleton_name="BODY";rig.bones={{"ROOT"},{"TIP",0,{0,0,2}}};
    Assets::ModelAnimationDesc clip;clip.name="MOVE";clip.skeleton_name="BODY";clip.frame_count=2;clip.frame_rate=30;
    clip.channels={{1,Assets::ModelChannelComponent::TranslationZ,0,true,{{0,0,0,0},{4,0,0,0}}}};rig.animations={clip};
    ModelAssetPose definition;std::string error;BOOST_REQUIRE(definition.Initialize(rig,error));auto first=definition,second=definition;
    rig.animations.front().channels.front().samples.back()[0]=999;
    BOOST_REQUIRE(first.Evaluate(0,1));BOOST_REQUIRE(second.Evaluate(0,.5f));RenderTransform initial,one,two;
    BOOST_REQUIRE(definition.Bone_Transform(1,initial));BOOST_REQUIRE(first.Bone_Transform(1,one));BOOST_REQUIRE(second.Bone_Transform(1,two));
    BOOST_TEST(initial.matrix[11]==2.f);BOOST_TEST(one.matrix[11]==6.f);BOOST_TEST(two.matrix[11]==4.f);
    BOOST_REQUIRE(first.Rest());BOOST_REQUIRE(second.Bone_Transform(1,two));BOOST_TEST(two.matrix[11]==4.f);
}
BOOST_AUTO_TEST_CASE(external_clip_bank_blends_local_poses_and_preserves_hierarchy_length) {
    Assets::ModelRigDesc rig;rig.skeleton_name="BODY";rig.bones={{"ROOT"},{"HINGE",0,{1,0,0}},{"TIP",1,{2,0,0}}};
    Assets::ModelAnimationDesc first;first.name="A";first.skeleton_name="BODY";first.frame_count=2;first.frame_rate=30;
    first.channels={{1,Assets::ModelChannelComponent::Rotation,0,true,{{0,0,0,1},{0,0,0,1}}}};
    auto second=first;second.name="B";second.channels.front().samples={{0,0,1,0},{0,0,1,0}};
    const std::array bank{first,second};ModelAssetPose pose;std::string error;BOOST_REQUIRE(pose.Initialize(rig,std::span<const Assets::ModelAnimationDesc>(bank),error));
    BOOST_REQUIRE(pose.Evaluate_Blended(0,0,1,0,.5f));RenderTransform tip;BOOST_REQUIRE(pose.Bone_Transform(2,tip));
    BOOST_CHECK_SMALL(tip.matrix[3]-1,0.0001f);BOOST_CHECK_SMALL(std::abs(tip.matrix[7])-2,0.0001f);
    BOOST_REQUIRE(pose.Evaluate_Blended(0,0,1,0,1));BOOST_REQUIRE(pose.Bone_Transform(2,tip));BOOST_CHECK_SMALL(tip.matrix[3]+1,0.0001f);
    BOOST_TEST(!pose.Evaluate_Blended(0,0,2,0,.5f));BOOST_TEST(!pose.Evaluate_Blended(0,0,1,0,1.1f));
}
BOOST_AUTO_TEST_CASE(external_clip_binding_preserves_attached_bones_and_original_pivot_contract) {
    Assets::ModelRigDesc rig;rig.skeleton_name="BODY";rig.bones={{"ROOT"},{"NECK",0,{0,0,2}},{"head/JAW",1,{1,0,0}}};
    Assets::ModelAnimationDesc clip;clip.name="IDLE";clip.skeleton_name="OTHER";clip.frame_count=2;clip.frame_rate=30;
    clip.channels={{1,Assets::ModelChannelComponent::TranslationZ,0,true,{{0,0,0,0},{2,0,0,0}}}};
    ModelAssetPose pose;std::string error;BOOST_REQUIRE(pose.Initialize(rig,clip,error));BOOST_REQUIRE(pose.Evaluate(0,.5f));
    RenderTransform jaw;BOOST_REQUIRE(pose.Bone_Transform(2,jaw));BOOST_TEST(jaw.matrix[11]==3.f);BOOST_TEST(jaw.matrix[3]==1.f);
    clip.channels.front().bone=99;BOOST_TEST(!pose.Initialize(rig,clip,error));BOOST_TEST(error.find("OTHER")!=std::string::npos);
    clip.skeleton_name="body";BOOST_REQUIRE(pose.Initialize(rig,clip,error));BOOST_REQUIRE(pose.Evaluate(0,1));BOOST_REQUIRE(pose.Bone_Transform(2,jaw));BOOST_TEST(jaw.matrix[11]==2.f);
    clip.channels.front().bone=1;clip.channels.front().component=Assets::ModelChannelComponent::RotationX;
    BOOST_REQUIRE(pose.Initialize(rig,clip,error));BOOST_REQUIRE(pose.Evaluate(0,1));BOOST_REQUIRE(pose.Bone_Transform(2,jaw));BOOST_TEST(jaw.matrix[11]==2.f);
}
BOOST_AUTO_TEST_CASE(independent_attachment_animation_preserves_body_pose_and_visibility) {
    Assets::ModelRigDesc rig;rig.skeleton_name="BODY";rig.bones={{"ROOT"},{"BODY",0},{"head/ROOT",1},{"head/JAW",2}};
    Assets::ModelAnimationDesc body;body.name="WALK";body.skeleton_name="BODY";body.frame_count=2;body.frame_rate=30;
    body.channels={{1,Assets::ModelChannelComponent::TranslationZ,0,true,{{4,0,0,0},{8,0,0,0}}},{1,Assets::ModelChannelComponent::Visibility,0,true,{{0,0,0,0}}}};
    Assets::ModelAnimationDesc face;face.name="TALK";face.skeleton_name="BODY";face.frame_count=2;face.frame_rate=30;
    face.channels={{3,Assets::ModelChannelComponent::TranslationX,0,true,{{0,0,0,0},{2,0,0,0}}}};
    rig.animations={body,face};ModelAssetPose pose;std::string error;BOOST_REQUIRE(pose.Initialize(rig,error));BOOST_REQUIRE(pose.Evaluate(0,.5f));
    BOOST_REQUIRE(pose.Evaluate_Layer(1,.5f,2,2));RenderTransform jaw;BOOST_REQUIRE(pose.Bone_Transform(3,jaw));BOOST_TEST(jaw.matrix[3]==1.f);BOOST_TEST(jaw.matrix[11]==6.f);BOOST_TEST(!pose.Visible(1));
    BOOST_TEST(!pose.Evaluate_Layer(1,0,4,1));BOOST_TEST(!pose.Evaluate_Layer(1,0,2,3));
    BOOST_REQUIRE(pose.Evaluate(0,0));BOOST_REQUIRE(pose.Bone_Transform(3,jaw));BOOST_TEST(jaw.matrix[3]==0.f);BOOST_TEST(jaw.matrix[11]==4.f);
}
BOOST_AUTO_TEST_CASE(samples_rest_relative_rotation_without_shrinking_at_half_frames) {
    Assets::ModelRigDesc rig;rig.skeleton_name="RIG";
    rig.bones={{"ROOT"},{"HINGE",0,{2,3,4}},{"TIP",1,{1,0,0}}};
    Assets::ModelAnimationDesc animation;animation.name="OPEN";animation.skeleton_name="RIG";animation.frame_count=2;animation.frame_rate=30;
    animation.channels={{1,Assets::ModelChannelComponent::Rotation,0,true,{{0,0,0,1},{0,0,1,0}}},
                        {1,Assets::ModelChannelComponent::TranslationZ,0,true,{{0,0,0,0},{4,0,0,0}}}};
    rig.animations={animation};ModelAssetPose pose;std::string error;
    BOOST_REQUIRE(pose.Initialize(rig,error));BOOST_REQUIRE(pose.Evaluate(0,.5f));
    RenderTransform tip;BOOST_REQUIRE(pose.Bone_Transform(2,tip));
    BOOST_TEST(tip.matrix[3]==2.f,boost::test_tools::tolerance(.0001f));
    BOOST_TEST(tip.matrix[7]==4.f,boost::test_tools::tolerance(.0001f));
    BOOST_TEST(tip.matrix[11]==6.f,boost::test_tools::tolerance(.0001f));
    BOOST_TEST(std::sqrt(tip.matrix[0]*tip.matrix[0]+tip.matrix[4]*tip.matrix[4]+tip.matrix[8]*tip.matrix[8])==1.f,boost::test_tools::tolerance(.0001f));
    BOOST_REQUIRE(pose.Evaluate(0,-.5f,true));BOOST_REQUIRE(pose.Bone_Transform(2,tip));
    BOOST_TEST(tip.matrix[11]==6.f,boost::test_tools::tolerance(.0001f));
}
BOOST_AUTO_TEST_CASE(uses_identity_motion_and_visibility_defaults_outside_sparse_ranges) {
    Assets::ModelRigDesc rig;rig.skeleton_name="RIG";rig.bones={{"ROOT"},{"HINGE",0}};
    Assets::ModelAnimationDesc animation;animation.name="OPEN";animation.skeleton_name="RIG";animation.frame_count=5;animation.frame_rate=30;
    animation.channels={{1,Assets::ModelChannelComponent::TranslationX,2,true,{{5,0,0,0},{7,0,0,0}}},
                        {1,Assets::ModelChannelComponent::Visibility,2,true,{{0,0,0,0}}}};
    rig.animations={animation};ModelAssetPose pose;std::string error;
    BOOST_REQUIRE(pose.Initialize(rig,error));BOOST_REQUIRE(pose.Evaluate(0,0));
    RenderTransform matrix;BOOST_REQUIRE(pose.Bone_Transform(1,matrix));BOOST_TEST(matrix.matrix[3]==0.f);BOOST_TEST(pose.Visible(1));
    BOOST_REQUIRE(pose.Evaluate(0,1.5f));BOOST_REQUIRE(pose.Bone_Transform(1,matrix));BOOST_TEST(matrix.matrix[3]==2.5f);
    BOOST_REQUIRE(pose.Evaluate(0,2));BOOST_TEST(!pose.Visible(1));
    BOOST_REQUIRE(pose.Evaluate(0,8));BOOST_REQUIRE(pose.Bone_Transform(1,matrix));BOOST_TEST(matrix.matrix[3]==0.f);BOOST_TEST(pose.Visible(1));
    rig.animations[0].channels_available=false;BOOST_REQUIRE(pose.Initialize(rig,error));BOOST_TEST(!pose.Evaluate(0,2));
}
