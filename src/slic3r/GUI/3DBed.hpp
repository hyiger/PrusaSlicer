///|/ Copyright (c) Prusa Research 2019 - 2023 Enrico Turri @enricoturri1966, Filip Sykala @Jony01, Vojtěch Bubník @bubnikv, Lukáš Matěna @lukasmatena
///|/
///|/ PrusaSlicer is released under the terms of the AGPLv3 or higher
///|/
#ifndef slic3r_3DBed_hpp_
#define slic3r_3DBed_hpp_

#include "GLTexture.hpp"
#include "3DScene.hpp"
#include "CoordAxes.hpp"
#include "MeshUtils.hpp"
#include "BedMeshData.hpp"

#include "libslic3r/BuildVolume.hpp"
#include "libslic3r/ExPolygon.hpp"

#include <tuple>
#include <array>

namespace Slic3r {
namespace GUI {

class GLCanvas3D;

class Bed3D
{
public:
    enum class Type : unsigned char
    {
        // The print bed model and texture are available from some printer preset.
        System,
        // The print bed model is unknown, thus it is rendered procedurally.
        Custom
    };

private:
    BuildVolume m_build_volume;
    Type m_type{ Type::Custom };
    std::string m_texture_filename;
    std::string m_model_filename;
    bool m_models_overlap;
    // Print volume bounding box exteded with axes and model.
    BoundingBoxf3 m_extended_bounding_box;
    // Print bed polygon
    ExPolygon m_contour;
    GLModel m_triangles;
    GLModel m_gridlines;
    GLModel m_contourlines;
    GLTexture m_texture;
    // temporary texture shown until the main texture has still no levels compressed
    GLTexture m_temp_texture;
    PickingModel m_model;
    Vec3d m_model_offset{ Vec3d::Zero() };
    CoordAxes m_axes;

    float m_scale_factor{ 1.0f };

    std::vector<std::unique_ptr<GLModel>> m_digits_models;
    std::unique_ptr<GLTexture> m_digits_texture;

    // Bed mesh overlay
    GLModel m_mesh_overlay;
    bool m_show_mesh_overlay{ false };
    BedMeshData m_mesh_data;
    float m_mesh_z_scale{ 200.f }; // exaggeration factor for visibility
    // Color map reference: Mean centers white on the average bed height
    // (highlights warp/tilt); Zero centers white on the nominal plane
    // (shows absolute leveling compensation magnitude).
    BedMeshData::Reference m_mesh_reference{ BedMeshData::Reference::Mean };

public:
    Bed3D() = default;
    ~Bed3D() = default;

    // Update print bed model from configuration.
    // Return true if the bed shape changed, so the calee will update the UI.
    //FIXME if the build volume max print height is updated, this function still returns zero
    // as this class does not use it, thus there is no need to update the UI.
    bool set_shape(const Pointfs& bed_shape, const double max_print_height, const std::string& custom_texture, const std::string& custom_model, bool force_as_custom = false);

    // Build volume geometry for various collision detection tasks.
    const BuildVolume& build_volume() const { return m_build_volume; }

    // Was the model provided, or was it generated procedurally?
    Type get_type() const { return m_type; }
    // Was the model generated procedurally?
    bool is_custom() const { return m_type == Type::Custom; }

    // Bounding box around the print bed, axes and model, for rendering.
    const BoundingBoxf3& extended_bounding_box() const { return m_extended_bounding_box; }

    void render(GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom, float scale_factor, bool show_texture);
    void render_axes();
    void render_for_picking(GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom, float scale_factor);

    // Bed mesh overlay
    void set_mesh_data(const BedMeshData& data);
    void set_show_mesh_overlay(bool show) { m_show_mesh_overlay = show; }
    bool is_mesh_overlay_shown() const { return m_show_mesh_overlay; }
    const BedMeshData& get_mesh_data() const { return m_mesh_data; }
    void set_mesh_z_scale(float scale) { m_mesh_z_scale = scale; invalidate_mesh_overlay(); }
    float get_mesh_z_scale() const { return m_mesh_z_scale; }

private:
    // Calculate an extended bounding box from axes and current model for visualization purposes.
    BoundingBoxf3 calc_extended_bounding_box() const;
    void init_triangles();
    void init_gridlines();
    void init_contourlines();
    void init_internal_model_from_file();
    static std::tuple<Type, std::string, std::string> detect_type(const Pointfs& shape);
    void render_internal(GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom, float scale_factor,
        bool show_texture, bool picking, bool active);
    void render_system(GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom, bool show_texture, bool is_active);
    void render_texture(bool bottom, GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool is_active);
    void render_model(const Transform3d& view_matrix, const Transform3d& projection_matrix);
    void render_custom(GLCanvas3D& canvas, const Transform3d& view_matrix, const Transform3d& projection_matrix, bool bottom, bool show_texture, bool picking, bool is_active);
    void render_default(bool bottom, bool picking, bool show_texture, const Transform3d& view_matrix, const Transform3d& projection_matrix);
    void render_contour(const Transform3d& view_matrix, const Transform3d& projection_matrix);

    void register_raycasters_for_picking(const GLModel::Geometry& geometry, const Transform3d& trafo);

    // Bed mesh overlay
    void init_mesh_overlay();
    void invalidate_mesh_overlay() { m_mesh_overlay.reset(); }
    void render_mesh_overlay(const Transform3d& view_matrix, const Transform3d& projection_matrix);

public:
    // ImGui legend for the bed mesh heatmap
    void render_mesh_legend();
};

} // GUI
} // Slic3r

#endif // slic3r_3DBed_hpp_
