#pragma once

#include "player.h"
#include "ai.h"
#include "particle.h"
#include "combat.h"

#include <iostream>
#include <vector>
#include <string>
#include <fstream>


enum class game_ui_state {START_MENU, IN_GAME, DEATH_MENU};
enum class attack_owner { PLAYER, AI };

class Game {

private:

std::vector<std::unique_ptr<base_attack>> player_attacks;
std::vector<std::unique_ptr<base_attack>> ai_attacks;

game_ui_state game_ui;
particle_system fx_system;
std::ofstream log_file;

float episode_reward_total = 0.0f;
int episode_length = 0;
int round_number = 0;

protected:

public:

Game();


void check_and_handle_auto_reset(Player &player, ai_combat_system &ai, int &current_step);
//^ debug

void initilize(bool &still_in_menu, Player &player); //start up menu

void update(float dt,Player &player, ai_combat_system &ai); //logic update
void draw(Player &player, ai_combat_system &ai); //update sprites

bool round_on_going(int &round_count, Player &player, ai_combat_system &ai, bool &had_lost); //round on going handels round and when it ends so it can run end_round with a if statement
void end_round(Player &player,  ai_combat_system &ai); //allows the player to upgrade their stats/upgrade abilities and lets the ai learn off of last round


void change_ui_state(game_ui_state new_state) {game_ui = new_state;}
game_ui_state get_game_ui_state() const {return game_ui;}

void spawn_player_attack(std::unique_ptr<base_attack> attack);
void spawn_ai_attack(std::unique_ptr<base_attack> attack);

void update_attacks(float dt, Vector2 player_pos, Vector2 ai_pos);
void draw_attacks();

const std::vector<std::unique_ptr<base_attack>>& get_player_attacks() const { return player_attacks; }
const std::vector<std::unique_ptr<base_attack>>& get_ai_attacks() const { return ai_attacks; }

};