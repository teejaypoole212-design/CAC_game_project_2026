#pragma once


#include "pch.h"
#include <iostream>
#include <array>

#include "player.h"
#include "ai.h"
#include "collision.h"

class arrow {

private:


Vector2 pos;
bool alive = true;

protected:

public:
collision_mask hitbox;
arrow() = default; 
arrow(Vector2 p)
: pos(p) {
    hitbox = collision_mask::make_circle(pos, 20.0f);
}

bool is_alive() const {return alive;}
Vector2 get_pos() const {return pos;}

void set_is_alive(bool change) {alive = change;}
void draw();
void update(Player &player, ai_combat_system &ai);
};


class arrow_handler {

private:
std::array<arrow, 8> arrow_list;
protected:

public:
arrow_handler() {
        const int screenWidth = 1920;
        const int screenHeight = 1080;

        // X pos for 4 arrows
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

void reset_arrows();
arrow return_arrow(int index) {return arrow_list[index];}

void draw_all();
void update_all(Player &player, ai_combat_system &ai);


void during_round(Player &player, ai_combat_system &ai);
void end_round();

};