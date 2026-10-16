#include "collision.h"
#include <raymath.h>
#include <cmath>
#include <algorithm>
#include <limits>

// =========================================================================
// constructors
// =========================================================================

collision_mask collision_mask::make_circle(Vector2 pos, float r) {
    collision_mask mask;
    mask.type = collision_shape_type::CIRCLE;
    mask.position = pos;
    mask.radius = r;
    return mask;
}

collision_mask collision_mask::make_rect(Vector2 pos, float hw, float hh) {
    collision_mask mask;
    mask.type = collision_shape_type::RECT;
    mask.position = pos;
    mask.half_width = hw;
    mask.half_height = hh;
    return mask;
}

collision_mask collision_mask::make_triangle(Vector2 pos, Vector2 o1, Vector2 o2, Vector2 o3) {
    collision_mask mask;
    mask.type = collision_shape_type::TRIANGLE;
    mask.position = pos;
    mask.tri_offset_1 = o1;
    mask.tri_offset_2 = o2;
    mask.tri_offset_3 = o3;
    return mask;
}

// =========================================================================
// Absolute-space geometry helpers
// =========================================================================

Rectangle collision_mask::get_absolute_rect() const {
    return Rectangle{
        position.x - half_width,
        position.y - half_height,
        half_width * 2.0f,
        half_height * 2.0f
    };
}

std::array<Vector2, 3> collision_mask::get_absolute_triangle() const {
    return {
        Vector2{ position.x + tri_offset_1.x, position.y + tri_offset_1.y },
        Vector2{ position.x + tri_offset_2.x, position.y + tri_offset_2.y },
        Vector2{ position.x + tri_offset_3.x, position.y + tri_offset_3.y }
    };
}

// =========================================================================
// Internal free-function helpers (not exposed in the header — pure geometry)
// =========================================================================

namespace {

// Closest point on a line segment [a,b] to point p.
Vector2 closest_point_on_segment(Vector2 p, Vector2 a, Vector2 b) {
    Vector2 ab = Vector2Subtract(b, a);
    float len_sq = Vector2LengthSqr(ab);
    if (len_sq <= 0.00001f) return a; // degenerate segment

    float t = Vector2DotProduct(Vector2Subtract(p, a), ab) / len_sq;
    t = std::clamp(t, 0.0f, 1.0f);
    return Vector2Add(a, Vector2Scale(ab, t));
}

// Distance from a point to the nearest edge of a triangle.
float distance_point_to_triangle_edges(Vector2 p, const std::array<Vector2, 3>& tri) {
    float d1 = Vector2Distance(p, closest_point_on_segment(p, tri[0], tri[1]));
    float d2 = Vector2Distance(p, closest_point_on_segment(p, tri[1], tri[2]));
    float d3 = Vector2Distance(p, closest_point_on_segment(p, tri[2], tri[0]));
    return std::min({d1, d2, d3});
}

// Generic polygon overlap test via the Separating Axis Theorem.
// Works for any convex polygons (used here for triangle-vs-triangle and
// triangle-vs-rect, treating the rect as a 4-point convex polygon).
bool polygons_intersect_sat(const std::vector<Vector2>& poly_a, const std::vector<Vector2>& poly_b) {
    auto test_axes = [&](const std::vector<Vector2>& poly) -> bool {
        size_t count = poly.size();
        for (size_t i = 0; i < count; ++i) {
            Vector2 p1 = poly[i];
            Vector2 p2 = poly[(i + 1) % count];

            // Edge vector, then the perpendicular (normal) is the candidate separating axis
            Vector2 edge = Vector2Subtract(p2, p1);
            Vector2 axis = { -edge.y, edge.x };

            float len = std::sqrt(axis.x * axis.x + axis.y * axis.y);
            if (len <= 0.00001f) continue;
            axis.x /= len;
            axis.y /= len;

            float min_a = std::numeric_limits<float>::max();
            float max_a = std::numeric_limits<float>::lowest();
            for (const auto& v : poly_a) {
                float proj = Vector2DotProduct(v, axis);
                min_a = std::min(min_a, proj);
                max_a = std::max(max_a, proj);
            }

            float min_b = std::numeric_limits<float>::max();
            float max_b = std::numeric_limits<float>::lowest();
            for (const auto& v : poly_b) {
                float proj = Vector2DotProduct(v, axis);
                min_b = std::min(min_b, proj);
                max_b = std::max(max_b, proj);
            }

            // Gap found on this axis -> shapes cannot be overlapping
            if (max_a < min_b || max_b < min_a) {
                return false; // separating axis found, so NOT intersecting
            }
        }
        return true; // no separating axis found among this polygon's edges
    };

    // Must find no separating axis among EITHER polygon's edges for a true overlap
    return test_axes(poly_a) && test_axes(poly_b);
}

std::vector<Vector2> rect_to_points(Rectangle r) {
    return {
        { r.x, r.y },
        { r.x + r.width, r.y },
        { r.x + r.width, r.y + r.height },
        { r.x, r.y + r.height }
    };
}

std::vector<Vector2> triangle_to_points(const std::array<Vector2, 3>& t) {
    return { t[0], t[1], t[2] };
}

} // anonymous namespace

// =========================================================================
// check_collision — dispatches on the pair of shape types
// =========================================================================

bool collision_mask::check_collision(const collision_mask& other) const {
    const collision_shape_type a_type = type;
    const collision_shape_type b_type = other.type;

    // --- CIRCLE vs CIRCLE ---
    if (a_type == collision_shape_type::CIRCLE && b_type == collision_shape_type::CIRCLE) {
        return CheckCollisionCircles(position, radius, other.position, other.radius);
    }

    // --- RECT vs RECT ---
    if (a_type == collision_shape_type::RECT && b_type == collision_shape_type::RECT) {
        return CheckCollisionRecs(get_absolute_rect(), other.get_absolute_rect());
    }

    // --- CIRCLE vs RECT (either order) ---
    if (a_type == collision_shape_type::CIRCLE && b_type == collision_shape_type::RECT) {
        return CheckCollisionCircleRec(position, radius, other.get_absolute_rect());
    }
    if (a_type == collision_shape_type::RECT && b_type == collision_shape_type::CIRCLE) {
        return CheckCollisionCircleRec(other.position, other.radius, get_absolute_rect());
    }

    // --- CIRCLE vs TRIANGLE (either order) ---
    if (a_type == collision_shape_type::CIRCLE && b_type == collision_shape_type::TRIANGLE) {
        auto tri = other.get_absolute_triangle();
        bool center_inside = CheckCollisionPointTriangle(position, tri[0], tri[1], tri[2]);
        if (center_inside) return true;
        return distance_point_to_triangle_edges(position, tri) <= radius;
    }
    if (a_type == collision_shape_type::TRIANGLE && b_type == collision_shape_type::CIRCLE) {
        auto tri = get_absolute_triangle();
        bool center_inside = CheckCollisionPointTriangle(other.position, tri[0], tri[1], tri[2]);
        if (center_inside) return true;
        return distance_point_to_triangle_edges(other.position, tri) <= other.radius;
    }

    // --- RECT vs TRIANGLE (either order) — SAT between rect-as-polygon and triangle ---
    if (a_type == collision_shape_type::RECT && b_type == collision_shape_type::TRIANGLE) {
        auto rect_pts = rect_to_points(get_absolute_rect());
        auto tri_pts = triangle_to_points(other.get_absolute_triangle());
        return polygons_intersect_sat(rect_pts, tri_pts);
    }
    if (a_type == collision_shape_type::TRIANGLE && b_type == collision_shape_type::RECT) {
        auto tri_pts = triangle_to_points(get_absolute_triangle());
        auto rect_pts = rect_to_points(other.get_absolute_rect());
        return polygons_intersect_sat(tri_pts, rect_pts);
    }

    // --- TRIANGLE vs TRIANGLE — SAT between both triangles ---
    if (a_type == collision_shape_type::TRIANGLE && b_type == collision_shape_type::TRIANGLE) {
        auto tri_a = triangle_to_points(get_absolute_triangle());
        auto tri_b = triangle_to_points(other.get_absolute_triangle());
        return polygons_intersect_sat(tri_a, tri_b);
    }

    // Should be unreachable — every type pair above is covered.
    return false;
}

// =========================================================================
// Debug drawing
// =========================================================================

void collision_mask::draw_debug(Color color) const {
    switch (type) {
        case collision_shape_type::CIRCLE:
            DrawCircleLinesV(position, radius, color);
            break;
        case collision_shape_type::RECT: {
            Rectangle r = get_absolute_rect();
            DrawRectangleLines(static_cast<int>(r.x), static_cast<int>(r.y),
                                static_cast<int>(r.width), static_cast<int>(r.height), color);
            break;
        }
        case collision_shape_type::TRIANGLE: {
            auto tri = get_absolute_triangle();
            DrawTriangleLines(tri[0], tri[1], tri[2], color);
            break;
        }
    }
}