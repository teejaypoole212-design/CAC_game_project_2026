#include "combat.h"
#include <raymath.h>
#include <cmath>

// =========================================================================
// meele_attack — a short-lived cone-shaped swing in front of the attacker
// =========================================================================

meele_attack::meele_attack(double dmg, double spd, double size, Color arc_clr, Color swd_clr)
    : base_attack(dmg, spd), arc_color(arc_clr), sword_color(swd_clr), swing_size(size) {
    // Real shape/offsets get set in initilize() once we know the facing
    // direction — this just needs to exist as a valid triangle up front.
    hitbox = collision_mask::make_triangle({0, 0}, {0, 0}, {0, 0}, {0, 0});
}

void meele_attack::initilize(Vector2 player_pos, float facing_direction) {
    pos = player_pos;
    direction = { cosf(facing_direction), sinf(facing_direction) };
    facing_angle_deg = facing_direction * RAD2DEG;

    float size = static_cast<float>(swing_size);
    float spread_angle = 35.0f * DEG2RAD; // half-angle of the cone, in radians

    // Grip point: a little out in front of the player along the facing
    // direction, so the swing reads as "held out" rather than centered on
    // the player's own body. Uses whichever is bigger: the usual proportional
    // offset, or just outside the owner's actual body (+ small padding) —
    // this is what keeps it from clipping into a bigger/differently-shaped body.
    float proportional_offset = size * grip_offset_mult;
    float min_clear_offset = owner_body_extent + 5.0f;
    float grip_offset = (proportional_offset > min_clear_offset) ? proportional_offset : min_clear_offset;
    Vector2 grip = { direction.x * grip_offset, direction.y * grip_offset };

    // Tip of the cone points straight out along the facing direction, from the grip
    Vector2 tip = { grip.x + direction.x * size, grip.y + direction.y * size };

    // Base corners spread to either side of the facing direction, from the grip.
    // These offsets are computed ONCE here and never touched again — this is
    // what makes the swing's direction stay fixed even as `pos` moves with the player.
    float left_angle = facing_direction - spread_angle;
    float right_angle = facing_direction + spread_angle;
    Vector2 base_left  = { grip.x + cosf(left_angle)  * (size * 0.5f), grip.y + sinf(left_angle)  * (size * 0.5f) };
    Vector2 base_right = { grip.x + cosf(right_angle) * (size * 0.5f), grip.y + sinf(right_angle) * (size * 0.5f) };

    hitbox.set_triangle_offsets(tip, base_left, base_right);
    hitbox.set_position(pos); // offsets above already account for the grip; position itself still follows pos

    lifetime = swing_duration;
}

void meele_attack::update(float dt, Vector2 player_pos) {
    lifetime -= dt;
    if (lifetime <= 0.0f) {
        kill(); // swing window has ended
        return;
    }

    // Follow the player: pos updates every frame, but direction/offsets
    // (set once in initilize()) never change, so the swing keeps pointing
    // the same way it did when it was thrown, just translated with the player.
    pos = player_pos;
    hitbox.set_position(pos);
}

void meele_attack::draw(Vector2 player_pos) {
    if (!is_Alive()) return;
    // player_pos unused here — `pos` is already kept in sync every frame by update(),
    // so drawing from `pos` is equivalent and keeps this function simple.

    float spread_deg = 60.0f;
    float progress = 1.0f - (lifetime / swing_duration); // 0 at swing start -> 1 at swing end
    if (progress < 0.0f) progress = 0.0f;
    if (progress > 1.0f) progress = 1.0f;

    // Ease-in-out quintic: slow startup, fast mid-swing, slow settle — the
    // higher power (vs. cubic) holds the ends a touch longer and blasts
    // through the middle noticeably faster, reading as a snappier swing
    // without changing the actual swing_duration.
    float eased_progress = (progress < 0.5f)
        ? 16.0f * progress * progress * progress * progress * progress
        : 1.0f - powf(-2.0f * progress + 2.0f, 5.0f) * 0.5f;

    // The stick's current angle sweeps from one side of the cone to the
    // other over the swing's lifetime, instead of sitting static — this is
    // what sells "quick swing" rather than "frozen pose".
    float start_angle_deg = facing_angle_deg - spread_deg;
    float current_angle_deg = start_angle_deg + (spread_deg * 2.0f * eased_progress);

    // Grip point in world space — same offset math as initilize(), recomputed
    // each frame so it stays glued to the player as they move mid-swing.
    float size = static_cast<float>(swing_size);
    float grip_offset = size * grip_offset_mult;
    Vector2 grip_world = { pos.x + direction.x * grip_offset, pos.y + direction.y * grip_offset };

    float reach = size * stick_length_mult;
    float current_rad = current_angle_deg * DEG2RAD;
    Vector2 stick_tip = { grip_world.x + cosf(current_rad) * reach, grip_world.y + sinf(current_rad) * reach };
    Vector2 blade_perp = { -sinf(current_rad), cosf(current_rad) };

    // Trailing arc that SHRINKS toward the tip as the swing progresses:
    // the trailing edge chases the leading edge, so the wedge starts full
    // width and tapers to a thin line right as the swing finishes — reads
    // like a real decelerating blade streak rather than a growing pie slice.
    float full_spread_deg = spread_deg * 2.0f;
    float trail_width_deg = full_spread_deg * (1.0f - eased_progress);
    float trail_start_deg = current_angle_deg - trail_width_deg;
    if (trail_start_deg < start_angle_deg) trail_start_deg = start_angle_deg; // don't overshoot the cone's own start

    // Fade the trail out over the last third of the swing, so it visibly
    // dissipates just before the swing ends instead of cutting off abruptly
    // — the fade itself reads as the motion finishing, making it feel quicker.
    float tail_fade = (progress > 0.7f) ? (1.0f - (progress - 0.7f) / 0.3f) : 1.0f;

    DrawCircleSector(grip_world, reach, trail_start_deg, current_angle_deg, 24, Fade(arc_color, 0.5f * tail_fade));
    DrawLineEx(grip_world, stick_tip, 2.0f, Fade(ColorBrightness(arc_color, 0.6f), 0.85f * tail_fade));

    float guard_width = size * 0.16f;
    Vector2 guard_left  = { grip_world.x + blade_perp.x * guard_width, grip_world.y + blade_perp.y * guard_width };
    Vector2 guard_right = { grip_world.x - blade_perp.x * guard_width, grip_world.y - blade_perp.y * guard_width };
    DrawLineEx(guard_left, guard_right, 3.0f, ColorBrightness(sword_color, -0.4f));

    Vector2 pommel = { grip_world.x - cosf(current_rad) * (size * 0.08f), grip_world.y - sinf(current_rad) * (size * 0.08f) };
    DrawCircleV(pommel, 3.0f, ColorBrightness(sword_color, -0.5f));

    // The stick itself — drawn on top of the arc, from the grip point (not the player's center).
    float blade_base_width = size * 0.09f;
    float blade_tip_width  = size * 0.02f;
    Vector2 blade_base_left  = { grip_world.x + blade_perp.x * blade_base_width, grip_world.y + blade_perp.y * blade_base_width };
    Vector2 blade_base_right = { grip_world.x - blade_perp.x * blade_base_width, grip_world.y - blade_perp.y * blade_base_width };
    Vector2 blade_tip_left  = { stick_tip.x + blade_perp.x * blade_tip_width, stick_tip.y + blade_perp.y * blade_tip_width };
    Vector2 blade_tip_right = { stick_tip.x - blade_perp.x * blade_tip_width, stick_tip.y - blade_perp.y * blade_tip_width };
    Vector2 blade_strip[4] = { blade_base_left, blade_tip_left, blade_base_right, blade_tip_right };
    DrawTriangleStrip(blade_strip, 4, sword_color);

    Vector2 highlight_tip = { grip_world.x + cosf(current_rad) * reach * 0.92f, grip_world.y + sinf(current_rad) * reach * 0.92f };
    DrawLineEx(grip_world, highlight_tip, 1.5f, ColorBrightness(sword_color, 0.5f));
    DrawCircleV(stick_tip, 2.5f, ColorBrightness(sword_color, 0.6f));
}

// =========================================================================
// ranged_attack — a projectile that travels outward from the attacker
// =========================================================================

ranged_attack::ranged_attack(double dmg, double spd, Color clr)
    : base_attack(dmg, spd), arrow_color(clr) {
    hitbox = collision_mask::make_rect({0, 0}, arrow_length / 2.0f, arrow_width / 2.0f);
}

void ranged_attack::initilize(Vector2 player_pos, float facing_direction) {
    pos = player_pos;
    direction = { cosf(facing_direction), sinf(facing_direction) };

    hitbox.set_position(pos);
    lifetime = max_lifetime;
}

void ranged_attack::update(float dt, Vector2 player_pos) {
    // player_pos intentionally unused — unlike the melee swing, an arrow
    // travels on its own trajectory once fired and doesn't track the shooter.
    lifetime -= dt;
    if (lifetime <= 0.0f) {
        kill();
        return;
    }

    float move_speed = static_cast<float>(get_speed());
    pos.x += direction.x * move_speed * dt;
    pos.y += direction.y * move_speed * dt;
    hitbox.set_position(pos);

    // Expire if it flies off the play area rather than lingering "alive" offscreen
    if (pos.x < -50.0f || pos.x > 1970.0f || pos.y < -50.0f || pos.y > 1130.0f) {
        kill();
    }
}

void ranged_attack::draw(Vector2 player_pos) {
    if (!is_Alive()) return;
    // player_pos intentionally unused — the arrow draws from its own
    // traveling pos, not the player's current position.
    // placeholder — swap for real arrow art later

    float angle = atan2f(direction.y, direction.x);
    Vector2 fwd = { cosf(angle), sinf(angle) };
    Vector2 perp = { -fwd.y, fwd.x };

    float half_len = static_cast<float>(arrow_length) * 0.5f;
    float half_wid = static_cast<float>(arrow_width) * 0.5f;

    Vector2 tail = { pos.x - fwd.x * half_len, pos.y - fwd.y * half_len };
    Vector2 head = { pos.x + fwd.x * half_len, pos.y + fwd.y * half_len };

    Color shaft_color = ColorBrightness(arrow_color, -0.35f);
    DrawLineEx(tail, head, half_wid * 0.9f, shaft_color);

    float fletch_len = half_len * 0.5f;
    float fletch_wid = half_wid * 1.6f;
    Vector2 fletch_root  = { tail.x + fwd.x * fletch_len, tail.y + fwd.y * fletch_len };
    Vector2 fletch_top    = { fletch_root.x + perp.x * fletch_wid, fletch_root.y + perp.y * fletch_wid };
    Vector2 fletch_bottom = { fletch_root.x - perp.x * fletch_wid, fletch_root.y - perp.y * fletch_wid };
    Color fletch_color = ColorBrightness(arrow_color, -0.15f);
    DrawTriangle(tail, fletch_top, fletch_root, fletch_color);
    DrawTriangle(tail, fletch_root, fletch_bottom, fletch_color);

    float head_len = half_wid * 2.2f;
    Vector2 arrow_tip = { head.x + fwd.x * head_len, head.y + fwd.y * head_len };
    Vector2 head_left  = { head.x + perp.x * half_wid * 1.4f, head.y + perp.y * half_wid * 1.4f };
    Vector2 head_right = { head.x - perp.x * half_wid * 1.4f, head.y - perp.y * half_wid * 1.4f };
    DrawTriangle(head_right, head_left, arrow_tip, arrow_color);
}