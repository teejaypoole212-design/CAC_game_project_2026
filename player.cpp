#include "player.h"
#include <cmath>
#include <algorithm>

Player::Player(std::string n, Shape shp, Color clr)
: name(n), shape(shp), color(clr), pos({0,0}), last_pos({0,0}),
  sword(static_cast<double>(damage), static_cast<double>(speed), 60.0, Fade(clr, 0.4f), clr) {
    // Build the hitbox to match the chosen shape, centered on pos
if (shape == Shape::circle) {
        hitbox = collision_mask::make_circle(pos, 20.0f);
    } else if (shape == Shape::rect) {
        hitbox = collision_mask::make_rect(pos, 20.0f, 20.0f);
    } else { // should be triangle  (if its not thats bad...)
        hitbox = collision_mask::make_triangle(pos, {0,-25}, {-20,15}, {20,15});
    }

    sword.set_owner_body_extent(get_body_extent());
}
//player action funcs 
void Player::move(float dt) {
last_pos = pos;
float dir_x = 0.0f;
float dir_y = 0.0f;

if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)) dir_y -= 1.0f * speed;
if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) dir_y += 1.0f * speed;
if (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT)) dir_x -= 1.0f * speed;
if (IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) dir_x += 1.0f * speed;

if (dir_x != 0.0f || dir_y != 0.0f) {
float length = std::sqrt(dir_x * dir_x + dir_y * dir_y);
        dir_x /= length;
        dir_y /= length;
    }

float speed = 300.0f; 
pos.x += dir_x * speed * dt;
pos.y += dir_y * speed * dt;
}

void Player::attack_meele(Game &game, float aim_angle) {
    if (meele_cooldown_current >= meele_cooldown_max) {
    if (weapon_state != WeaponState::sword_idle) return;

    sword.set_owner_body_extent(get_body_extent()); // in case shape changed since construction
    sword.initilize(pos, facing_angle);
    weapon_state = WeaponState::sword_swinging;
    is_attacking = true;

    auto swing = std::make_unique<meele_attack>(10 + damage, 5 , 60, YELLOW, DARKBROWN);
    swing->initilize(pos, aim_angle);
    game.spawn_player_attack(std::move(swing));
    meele_cooldown_current = 0.0f;
    std::cout << meele_cooldown_current << "current     " << meele_cooldown_max << "    max, should occure" << std::endl;
    }
}
void Player::attack_ranged(Game &game, float aim_angle) { //honestly dont need cool down on ranged, the player wont have too many arrows any ways
    if (weapon_state != WeaponState::sword_idle) return;
    if (ranged_projectile_count <= 0) return;

    // Don't fire immediately — the sword needs to be put away first.
    weapon_state = WeaponState::sheathing;
    weapon_timer = sheathe_delay;
    is_attacking = true;


    auto arrow = std::make_unique<ranged_attack>(10, 400, SKYBLUE);
    arrow->initilize(pos, aim_angle);
    game.spawn_player_attack(std::move(arrow));

}


void Player::flash_red(float seconds) {
    flash_timer = seconds;
    //std::cout << "this is a test to see if flashing workign :)" << std::endl;
}

void Player::tick_flash(float dt) {
    if (flash_timer > 0.0f)
        flash_timer = std::max(0.0f, flash_timer - dt);
}

bool Player::is_flashing() const {
    return flash_timer > 0.0f;
}



//checks
//bool Player::check_collisions() {
//    return false;
//}
//dosnt matter any more 

//misc
void Player::update(float dt) {

tick_flash(dt);
last_pos = pos;
hitbox.set_position(pos);
this->move(dt); 

meele_cooldown_current += dt;


    Vector2 mouse = GetMousePosition();
    facing_angle = atan2f(mouse.y - pos.y, mouse.x - pos.x);

    if (weapon_state == WeaponState::sword_swinging) {
        sword.update(dt, pos);
        if (!sword.is_Alive()) {
            weapon_state = WeaponState::sword_idle;
            is_attacking = false;
        }
    } else if (weapon_state == WeaponState::sheathing) {
        weapon_timer -= dt;
        if (weapon_timer <= 0.0f) {
            ranged_projectile_count -= 1;
            arrows.emplace_back(static_cast<double>(damage), 600.0, BEIGE);
            arrows.back().initilize(pos, facing_angle);
            weapon_state = WeaponState::sword_idle;
            is_attacking = false;
        }
    }

    for (auto& arrow : arrows) {
        if (arrow.is_Alive()) arrow.update(dt, pos);
    }
    arrows.erase(std::remove_if(arrows.begin(), arrows.end(),
                                 [](const ranged_attack& a) { return !a.is_Alive(); }),
                 arrows.end());
}

void Player::draw()
{

    Color c = is_flashing() ? RED : color;

    if (!this->check_is_alive()) return;

    if (shape == Shape::circle) {
        // border
        DrawCircleV(pos, 22.0f, BLACK);
        // fill
        DrawCircleV(pos, 20.0f, color);
    }
    else if (shape == Shape::rect) {
        // border
        DrawRectangle(static_cast<int>(pos.x) - 22, static_cast<int>(pos.y) - 22, 44, 44, BLACK);
        // fill
        DrawRectangle(static_cast<int>(pos.x) - 20, static_cast<int>(pos.y) - 20, 40, 40, color);
    }
    else if (shape == Shape::triangle) {
        // border larger
        Vector2 b1 = { pos.x,         pos.y - 28.0f };
        Vector2 b2 = { pos.x - 23.0f, pos.y + 17.0f };
        Vector2 b3 = { pos.x + 23.0f, pos.y + 17.0f };
        DrawTriangle(b1, b2, b3, BLACK);

        // fill
        Vector2 v1 = { pos.x,         pos.y - 25.0f };
        Vector2 v2 = { pos.x - 20.0f, pos.y + 15.0f };
        Vector2 v3 = { pos.x + 20.0f, pos.y + 15.0f };
        DrawTriangle(v1, v2, v3, color);
    }
    // ----- health bar -----
    const float bar_width  = 60.0f;
    const float bar_height = 8.0f;
    const float bar_x = pos.x - bar_width * 0.5f;
    const float bar_y = pos.y - 40.0f;               // sits above the player

    float hp_percent = (float)health / (float)max_health;
    if (hp_percent < 0.0f) hp_percent = 0.0f;
    if (hp_percent > 1.0f) hp_percent = 1.0f;

    // background (dark)
    DrawRectangle((int)bar_x, (int)bar_y, (int)bar_width, (int)bar_height, Color{30, 30, 30, 220});

    // foreground (shrinks as HP drops)
    Color hp_color;
    if (hp_percent > 0.6f)      hp_color = Color{0, 220, 80, 255};   // green
    else if (hp_percent > 0.3f) hp_color = Color{240, 200, 0, 255};  // yellow
    else                        hp_color = Color{220, 40, 40, 255};  // red

    DrawRectangle((int)bar_x, (int)bar_y, (int)(bar_width * hp_percent), (int)bar_height, hp_color);

    // thin outline
    DrawRectangleLines((int)bar_x, (int)bar_y, (int)bar_width, (int)bar_height, Color{200, 200, 200, 180});

    // name above the bar
    int name_width = MeasureText(name.c_str(), 16);
    DrawText(name.c_str(),
             (int)(pos.x - name_width * 0.5f),
             (int)(bar_y - 18),
             16, BLACK); //idk wht it looks a little difrrent

    // ----- weapons -----
    if (weapon_state == WeaponState::sword_swinging) {
        sword.draw(pos);
    }

    for (auto& arrow : arrows) {
        arrow.draw(pos);
    }
}


void Player::take_damage(int amount) {
    health -= amount;
    flash_red(0.5f);
}