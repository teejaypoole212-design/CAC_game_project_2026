#pragma once

#include "collision.h"
#include "raylib.h"

class base_attack {

private:

double speed;
double damage;
bool is_alive = true;

float cooldown_max = 2.0f; //can be changed later if player gets better attack speed
float cooldown_min = 2.0f; //current progress tords next attack
//^ += dt and when attack happens cld_min = 0 until = > than max to allow next attack



protected:

Vector2 pos;
Vector2 direction;
collision_mask hitbox;

public:
base_attack(double dmg, double spd) : speed(spd), damage(dmg), pos({0,0}), direction({0,0}) {}

virtual ~base_attack() = default;

virtual void draw(Vector2 player_pos) = 0;
virtual void update(float dt, Vector2 player_pos) = 0;
virtual void initilize(Vector2 player_pos, float direction) =0; //when the attack is first sent out/done

void kill() {is_alive = false;}
bool is_Alive() const {return is_alive;}

double get_speed() const {return speed;}
double get_damage() const {return damage;}
Vector2 get_pos() const {return pos;}
Vector2 get_dir() const {return direction;}
bool can_attack(float dt) {if(cooldown_min >= cooldown_max) {cooldown_min = 0.0f; return true;}else{cooldown_min += dt; return false;}}

void apply_damage_mult(int add) {damage += add;}; //this will be applied right after creation, right before it is drawn and occured
void apply_speed_mult(int add) {speed += add;}; //probaly wont be used 

const collision_mask& get_hitbox() const {return hitbox;}

};

class meele_attack : public base_attack {

private:
Color arc_color;
Color sword_color;
double swing_size;

float lifetime = 0.0f;
const float swing_duration = 0.20f;     // how long the swing stays active once thrown
float facing_angle_deg = 0.0f;          // stored so draw() can animate the sweep without recomputing from direction
const float stick_length_mult = 1.0f;        
const float grip_offset_mult = 0.35f;  // how far the visible stick reaches, relative to swing_size
float owner_body_extent = 20.0f;
protected:

public:
    meele_attack(double dmg, double spd /*swing speed (also kind of attack speed bc swinging ends quicker)*/,double size, Color arc_clr, Color swd_clr);

    void draw(Vector2 player_pos) override; //no need to plug player dir into draw bc initilize will make its own dir = the corerct dir from player
    void update(float dt, Vector2 player_pos) override; //use a triangle collision to best suit the cone shape of the swing

    void initilize(Vector2 player_pos, float direction) override;

    void set_owner_body_extent(float extent) { owner_body_extent = extent; }
};

class ranged_attack : public base_attack {
private:
float arrow_width = 6.0f;
float arrow_length = 24.0f;
Color arrow_color;

float max_lifetime = 2.0f;   // arrow expires after this long even if it hits nothing
float lifetime = 0.0f;

protected:
public:
    ranged_attack(double dmg, double spd /*how quick the arrow moves*/,Color clr);

    void draw(Vector2 player_pos) override; //has to match decleration just wont use player_pos (is going to use own pos)
    void update(float dt, Vector2 player_pos) override;

    void initilize(Vector2 player_pos, float direction) override;
};