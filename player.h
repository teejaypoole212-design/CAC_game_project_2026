#pragma once

#include "pch.h"

#include "ai.h"
#include "game.h"
#include "collision.h"
#include "combat.h"
#include "types.h"

#include <string>
#include <vector>

class Game;
class ai_combat_system;
using AI = ai_combat_system;


enum class WeaponState {sword_idle, sword_swinging, sheathing};

class Player {
private:

Vector2 pos = {1860.0f, 540.0f};
Vector2 last_pos = {0.0f,0.0f}; //place holder 

//stats
int health = 100; // the 1's in spd and dmg are the level of stat
int speed = 1;
int damage = 0;
int max_health = 100;
float meele_speed = 0; // the amount of time taken off of cool down

int health_mult = 0;
int speed_mult = 0;
int damage_mult = 0;
int meele_speed_mult = 0;

//======
collision_mask hitbox;

int ranged_projectile_count = 0;

std::string name;
Shape shape;
Color color; //allows the player to customize their characters looks

bool is_attacking = false;
bool is_grounded = true;

// --- weapon/attack state ---
WeaponState weapon_state = WeaponState::sword_idle;
float weapon_timer = 0.0f;
const float sheathe_delay = 0.25f; // time to put the sword away before an arrow actually fires
float facing_angle = 0.0f;         // radians, aimed at the mouse every frame

float meele_cooldown_current = 0.0f;
float meele_cooldown_max = 0.75f;

meele_attack sword;
std::vector<ranged_attack> arrows;

protected:

public:

Player(std::string n, Shape shp, Color clr);

//setters
void set_name(std::string new_name) {name = new_name;}
void set_color(Color new_color) {color = new_color;}
void set_shape(Shape new_shape) {shape = new_shape;}
void set_position(Vector2 new_pos) {pos = new_pos;}

void reset_mults() {

    health -= health_mult; //to remove previos buffs
    speed -= speed_mult;
    damage -= damage_mult;
    meele_speed -= meele_speed_mult;

    health_mult = 0;
    speed_mult = 1;
    damage_mult = 0;
    meele_speed_mult = 0;
}

void apply_mults() {
    health += health_mult;
    speed += speed_mult;
    damage += damage_mult;
    meele_speed += meele_speed_mult;
}

void add_health_mult(int add) {health_mult += add;}
void add_speed_mult(float add) {speed_mult += add;}
void add_damage_mult(int add) {damage_mult += add;}
void add_meele_speed_mult(float add) {speed_mult += add;}

void take_damage(int amount) {health -= amount;}
void add_projectile(int a) {ranged_projectile_count += a;} 
//getters
int get_health() const {return health;}
int get_speed() const {return speed;}
int get_damage() const {return damage;}
std::string get_name() const {return name;}
Shape get_shape() const {return shape;}
Color get_color() const {return color;}
int get_ranged_amount() const {return ranged_projectile_count;}
int get_max_health() const {return max_health;}
Vector2 get_pos() const {return pos;}
Vector2 get_position() const {return pos;}
Vector2 get_last_pos() const {return last_pos;}
const collision_mask& get_hitbox() const { return hitbox; }




float get_meele_cooldown_current() {return meele_cooldown_current;}
float get_meele_cooldown_max() const {return meele_cooldown_max;}

//misc funcs

void move(float dt);
void update(float dt);
void draw();
void attack_meele(Game &game, float aim_angle); //paramaters unkown for now (i know now :) )
void attack_ranged(Game &game, float aim_angle); //paramaters unkown for now

//bool check_collisions(/*target against player*/);
bool check_is_alive() {if(health > 0) {return true;}else{return false;}}

float get_body_extent() const {
switch (shape) {
case Shape::circle:   return 20.0f;
case Shape::rect:     return 28.5f; // ~half-diagonal of the 40x40 square, so corners are covered too
case Shape::triangle: return 25.0f;
    }
return 20.0f;
}

};