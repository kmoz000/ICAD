#pragma once

#include "icad/compiler/diagnostics/diagnostic.hpp"
#include "icad/compiler/units/units.hpp"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace icad::compiler::ir {

struct TolerancePolicy {
    double linear_mm{1.0e-9};
    double angular_degrees{1.0e-7};
};

struct Quantity {
    double value{};
    std::string unit;
    units::Dimension dimension{units::Dimension::unknown};
};

struct Parameter {
    std::string name;
    Quantity value;
    std::string expression;
    std::vector<std::string> dependencies;
};

struct NamedAngle {
    std::string name;
    double degrees{};
};

enum class SpatialPointKind { absolute, offset };

struct SpatialPoint {
    std::string name;
    std::array<double, 3> position_mm{};
    SpatialPointKind kind{SpatialPointKind::absolute};
    std::string base_point;
    std::string direction;
    double distance_mm{};
    std::string distance_reference;
};

enum class DirectionKind { components, between_points, rotated };

struct Direction {
    std::string name;
    std::array<double, 3> unit{};
    DirectionKind kind{DirectionKind::components};
    std::string from_point;
    std::string to_point;
    std::string source_direction;
    std::string around_axis;
    double angle_degrees{};
    std::string angle_reference;
};

struct Transform {
    std::array<double, 3> position_mm{};
    std::array<double, 3> rotation_deg{};
};

struct BodyPose {
    std::string body;
    std::string point;
    Transform transform;
};

struct ComponentInstance {
    std::string name;
    std::string body;
    std::string point;
    Transform transform;
};

enum class JointKind { fixed, revolute, prismatic };

struct Joint {
    std::string name;
    JointKind kind{JointKind::fixed};
    std::string parent_body;
    std::string child_body;
    std::string point;
    std::string axis;
    double value{};
    double lower_limit{};
    double upper_limit{};
    std::string unit;
    std::string value_reference;
    std::string lower_limit_reference;
    std::string upper_limit_reference;
    bool driven{};
};

struct Property {
    std::string name;
    Quantity value;
    std::string expression;
};

struct Point2 {
    double x_mm{};
    double y_mm{};
};

enum class ProfileSegmentKind { line, circular_arc };

struct ProfileSegment {
    ProfileSegmentKind kind{ProfileSegmentKind::line};
    Point2 start;
    Point2 end;
    Point2 center;
    double radius_mm{};
    double sweep_radians{};
};

struct Profile {
    std::string name;
    std::vector<ProfileSegment> segments;
    std::vector<Point2> points;
};

struct SketchPoint {
    std::string name;
    Point2 initial;
    Point2 solved;
    bool fixed{};
};

struct SketchConstraint {
    std::string name;
    std::string kind;
    std::vector<std::string> references;
    double target_value{};
    std::string target_unit;
    std::string target_reference;
};

struct SketchEntity {
    std::string name;
    ProfileSegmentKind kind{ProfileSegmentKind::line};
    std::string start;
    std::string end;
    std::string center;
    bool counterclockwise{};
};

enum class SketchSolveStatus { fully_constrained, under_constrained, inconsistent };

struct Sketch {
    std::string name;
    std::string body;
    std::string plane{"XY"};
    std::string support_feature;
    std::string support_face;
    std::vector<SketchPoint> points;
    std::vector<SketchEntity> entities;
    std::vector<SketchConstraint> constraints;
    SketchSolveStatus status{SketchSolveStatus::under_constrained};
    std::size_t degrees_of_freedom{};
    std::size_t iterations{};
    double maximum_residual{};
};

enum class FeatureOperation { create, unite, cut, intersect };

struct Feature {
    std::string name;
    std::string source_keyword{"FEATURE"};
    std::string type;
    std::string profile;
    std::string target_profile;
    FeatureOperation operation{FeatureOperation::create};
    std::string selected_edge_point;
    std::string direction;
    std::string plane_point;
    std::string plane_normal;
    std::string sketch_plane{"XY"};
    std::string support_feature;
    std::string support_face;
    std::vector<std::string> path_points;
    std::size_t count{};
    std::vector<Property> properties;
};

struct Material {
    std::string name;
    std::string preset;
    std::array<double, 4> base_color{};
    double metallic{};
    double roughness{};
    std::string texture;
    unsigned int texture_seed{};
    double texture_scale_mm{100.0};
    std::string uv_mode{"BOX"};
};

struct Body {
    std::string name;
    std::string material;
    std::vector<Feature> features;
};

struct Constraint {
    std::string name;
    std::string kind;
    std::string first_body;
    std::string second_body;
    double minimum_mm{};
    double target_value{};
    std::string target_unit;
    std::string target_reference;
};

enum class MateKind { face, edge };

struct Mate {
    std::string name;
    MateKind kind{MateKind::face};
    std::string first_occurrence;
    std::string first_selector;
    std::string second_occurrence;
    std::string second_selector;
    double target_mm{};
    std::string target_reference;
};

struct Keyframe {
    double time_seconds{};
    Transform transform;
    double joint_value{};
    std::string joint_unit;
    bool visible{true};
};

struct AnimationTrack {
    std::string name;
    std::string target_kind;
    std::string target;
    std::string easing{"LINEAR"};
    std::vector<Keyframe> keyframes;
};

struct SceneLight {
    std::string name;
    std::string kind;
    std::array<double, 3> color{};
    double intensity{};
    std::array<double, 3> position_mm{};
};

struct SceneEvent {
    double time_seconds{};
    std::string name;
};

struct Scene {
    std::string name;
    double duration_seconds{};
    double frames_per_second{};
    std::string background;
    std::size_t loop_count{1};
    std::vector<SceneLight> lights;
    std::vector<SceneEvent> events;
    std::vector<AnimationTrack> tracks;
};

struct Project {
    std::string name;
    std::string canonical_length_unit;
    TolerancePolicy tolerance;
    std::vector<Parameter> parameters;
    std::vector<NamedAngle> angles;
    std::vector<SpatialPoint> points;
    std::vector<Direction> vectors;
    std::vector<BodyPose> poses;
    std::vector<ComponentInstance> instances;
    std::vector<Joint> joints;
    std::vector<Material> materials;
    std::vector<Profile> profiles;
    std::vector<Sketch> sketches;
    std::vector<Body> bodies;
    std::vector<Constraint> constraints;
    std::vector<Mate> mates;
    std::vector<Scene> scenes;
};

} // namespace icad::compiler::ir
