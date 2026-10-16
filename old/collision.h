#pragma once

#include <raylib.h>
#include <array>
#include <vector>

// Supported collision shape types.
enum class collision_shape_type {
    CIRCLE,
    RECT,
    TRIANGLE
};

// A collision_mask represents one shape's hitbox. It stores a reference
// "position" (treated as the shape's logical center — matching how Player
// draws circles/rects/triangles centered on its pos), plus shape-specific
// dimensions stored relative to that center.
//
// Usage:
//   collision_mask ai_hitbox = collision_mask::make_circle({100,100}, 20.0f);
//   collision_mask player_hitbox = collision_mask::make_rect({200,200}, 20.0f, 20.0f);
//   if (ai_hitbox.check_collision(player_hitbox)) { ... }
//
// You can also change shape/size on the fly with the setters, and update
// position every frame with set_position() without reconstructing the mask.
class collision_mask {
private:
    collision_shape_type type = collision_shape_type::CIRCLE;
    Vector2 position = {0.0f, 0.0f}; // logical center of the shape

    // --- Shape-specific data (only the relevant fields are used, based on `type`) ---

    // CIRCLE
    float radius = 0.0f;

    // RECT (stored as half-extents from `position`, so position is the rect's center)
    float half_width = 0.0f;
    float half_height = 0.0f;

    // TRIANGLE (stored as offsets from `position`, so position is a reference point —
    // NOT necessarily the centroid, this just matches however you originally built the triangle)
    Vector2 tri_offset_1 = {0.0f, -25.0f};
    Vector2 tri_offset_2 = {-20.0f, 15.0f};
    Vector2 tri_offset_3 = {20.0f, 15.0f};

    // Internal helpers to get absolute-space geometry for the current position
    Rectangle get_absolute_rect() const;
    std::array<Vector2, 3> get_absolute_triangle() const;

public:
    collision_mask() = default;

    // --- Factory constructors (clearer call sites than a raw constructor + type flag) ---
    static collision_mask make_circle(Vector2 pos, float radius);
    static collision_mask make_rect(Vector2 pos, float half_width, float half_height);
    static collision_mask make_triangle(Vector2 pos, Vector2 offset_1, Vector2 offset_2, Vector2 offset_3);

    // --- Core API ---

    // Checks collision against another mask, correctly dispatching based on
    // BOTH masks' declared types (e.g. this=CIRCLE vs other=RECT, or
    // this=TRIANGLE vs other=TRIANGLE, etc.) — all 6 unordered type pairs are handled.
    bool check_collision(const collision_mask& other) const;

    // --- Position (call this every frame to keep the mask following its owner) ---
    void set_position(Vector2 new_pos) { position = new_pos; }
    Vector2 get_position() const { return position; }

    // --- Type / shape info ---
    collision_shape_type get_type() const { return type; }

    // --- Shape-specific setters (only meaningful for the matching type, but
    // safe to call regardless — they just won't be used unless you also
    // change the type via a make_* factory or set_type). ---
    void set_radius(float r) { radius = r; }
    float get_radius() const { return radius; }

    void set_rect_half_extents(float hw, float hh) { half_width = hw; half_height = hh; }
    float get_half_width() const { return half_width; }
    float get_half_height() const { return half_height; }

    void set_triangle_offsets(Vector2 o1, Vector2 o2, Vector2 o3) {
        tri_offset_1 = o1; tri_offset_2 = o2; tri_offset_3 = o3;
    }

    // Allows converting an existing mask to a different shape type in place,
    // e.g. if an entity's hitbox shape changes at runtime.
    void set_type(collision_shape_type new_type) { type = new_type; }

    // Debug draw — draws the mask's current shape as an outline, useful for
    // visually verifying hitboxes line up with what's actually being rendered.
    void draw_debug(Color color = LIME) const;
};