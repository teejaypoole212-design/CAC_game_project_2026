#include "arrow_handler.h"


//arrow
void arrow::draw() {
    if (!alive) return;

    const float angle = (float)GetTime() * 0.25f + pos.x * 0.007f;   // + a small unique offset per arrow
    const float c = cosf(angle), s = sinf(angle);


    auto at = [&](float x, float y, float shift) -> Vector2 {
        return { pos.x + shift + x * c - y * s, pos.y + shift + x * s + y * c };
    };

    auto tri = [](Vector2 a, Vector2 b, Vector2 d, Color col) {
        const float cross = (b.x - a.x) * (d.y - a.y) - (b.y - a.y) * (d.x - a.x);
        if (cross > 0.0f) std::swap(b, d);
        DrawTriangle(a, b, d, col);
    };

    auto draw_arrow = [&](float shift, Color head, Color shaft, Color feather) {
        // shaft
        DrawLineEx(at(0, 14, shift), at(0, -5, shift), 3.0f, shaft);

        // swept-back arrowhead (two triangles around a notch, so it is more than a plain triangle)
        tri(at(0, -17, shift), at(-7.5f, -3, shift), at(0, -6.5f, shift), head);
        tri(at(0, -17, shift), at(0, -6.5f, shift),   at(7.5f, -3, shift), head);

        // feathers at the back (two pairs, swept outward)
        DrawLineEx(at(0, 7,  shift), at(-4.5f, 12.5f, shift), 3.0f, feather);
        DrawLineEx(at(0, 7,  shift), at( 4.5f, 12.5f, shift), 3.0f, feather);
        DrawLineEx(at(0, 11, shift), at(-4.5f, 16.5f, shift), 3.0f, feather);
        DrawLineEx(at(0, 11, shift), at( 4.5f, 16.5f, shift), 3.0f, feather);
    };

    const Color shadow = Fade(BLACK, 0.18f);
    draw_arrow(3.0f, shadow, shadow, shadow);                 // soft drop shadow
    draw_arrow(0.0f, BLUE, DARKBLUE, SKYBLUE);                // the arrow
}


void arrow::update(Player &player, ai_combat_system &ai) {

if (!alive) return;

ai.report_arrow(pos);         

hitbox.set_position(pos);

if (player.get_hitbox().check_collision(hitbox)) {
        //need to make the player pick it up
        player.add_projectile(1);
        alive = false;            // arrow is spent
    } else if (ai.get_hitbox().check_collision(hitbox)) {
        //ai picks it up
        ai.add_arrow(1);
        ai.on_arrow_pickup();
        alive = false;
    }


}

//handler
void arrow_handler::reset_arrows() {
int screenWidth = 1920;
int screenHeight = 1080;

        // X positions for 4 arrows
int x_positions[4] = {384, 768, 1152, 1536};

        // Top row (y = 100)
for (int i = 0; i < 4; i++) {
            arrow_list[i] = arrow({(float)x_positions[i], 100});
        }

        // Bottom row (y = 980)
for (int i = 0; i < 4; i++) {
            arrow_list[i+4] = arrow({(float)x_positions[i], (float)screenHeight - 100});
        }
}

void arrow_handler::update_all(Player &player, ai_combat_system &ai) {
for (auto &a : arrow_list) {
        a.update(player, ai);
    }
}

void arrow_handler::draw_all() {
for (auto &a : arrow_list) {
        a.draw();
    }
}

void arrow_handler::during_round(Player &player, ai_combat_system &ai) {
update_all(player, ai);
draw_all();
}

void arrow_handler::end_round() {
reset_arrows();
}