#include "arrow_handler.h"


//arrow
void arrow::draw() {
    if (alive) {
    Vector2 v1 = {pos.x, pos.y};
    Vector2 v2 = {pos.x - 10, pos.y + 30};
    Vector2 v3 = {pos.x + 10, pos.y + 30};
    DrawTriangle(v1, v2, v3, BLUE);
    }
}

void arrow::update(Player &player, ai_combat_system &ai) {

    if (!alive) return;

    
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