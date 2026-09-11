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

if (IsKeyDown(KEY_W)) dir_y -= 1.0f;
if (IsKeyDown(KEY_S)) dir_y += 1.0f;
if (IsKeyDown(KEY_A)) dir_x -= 1.0f;
if (IsKeyDown(KEY_D)) dir_x += 1.0f;

if (dir_x != 0.0f || dir_y != 0.0f) {
float length = std::sqrt(dir_x * dir_x + dir_y * dir_y);
        dir_x /= length;
        dir_y /= length;
    }

float speed = 300.0f; 
pos.x += dir_x * speed * dt;
pos.y += dir_y * speed * dt;
}

void Player::attack_meele() {
    if (weapon_state != WeaponState::sword_idle) return;

    sword.set_owner_body_extent(get_body_extent()); // in case shape changed since construction
    sword.initilize(pos, facing_angle);
    weapon_state = WeaponState::sword_swinging;
    is_attacking = true;
}
void Player::attack_ranged() {
    if (weapon_state != WeaponState::sword_idle) return;
    if (ranged_projectile_count <= 0) return;

    // Don't fire immediately — the sword needs to be put away first.
    weapon_state = WeaponState::sheathing;
    weapon_timer = sheathe_delay;
    is_attacking = true;
}

//checks
//bool Player::check_collisions() {
//    return false;
//}
//dosnt matter any more 

//misc
void Player::update(float dt) {
last_pos = pos;
hitbox.set_position(pos);
this->move(dt); 

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

void Player::draw() {
if (this->check_is_alive()) {
if (shape == Shape::circle) {
DrawCircleV(pos, 20.0f, color);
        } 
else if (shape == Shape::rect) {
DrawRectangle(static_cast<int>(pos.x) - 20, static_cast<int>(pos.y) - 20, 40, 40, color);
        } 
else if (shape == Shape::triangle) {
Vector2 v1 = { pos.x, pos.y - 25.0f };
Vector2 v2 = { pos.x - 20.0f, pos.y + 15.0f };
Vector2 v3 = { pos.x + 20.0f, pos.y + 15.0f };
DrawTriangle(v1, v2, v3, color);
        }
    }

    if (weapon_state == WeaponState::sword_swinging) {
        sword.draw(pos);
    } 

    for (auto& arrow : arrows) {
        arrow.draw(pos);
    }
}