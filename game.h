#pragma once

#include "player.h"
#include "ai.h"
#include "particle.h"
#include "combat.h"
#include "arrow_handler.h"
#include "render_ui.h"
#include "types.h"

#include <iostream>
#include <vector>
#include <string>
#include <fstream>
#include <memory>
#include <unordered_set>

enum class game_ui_state {START_MENU,SETUP_MENU, IN_GAME, DEATH_MENU};
enum class attack_owner { PLAYER, AI };

// main.cpp flips this (Q = fast training). While on, a scripted sparring partner pilots the Player.
extern bool g_sparring_enabled;
extern sparring_bot g_sparring;

// Game now only OWNS the shared state (attacks, effects, log, round lifecycle).
// All per-frame update / render logic lives in render_ui.cpp.
class Game {

private:

std::vector<std::unique_ptr<base_attack>> player_attacks;
std::vector<std::unique_ptr<base_attack>> ai_attacks;
std::unordered_set<const base_attack*> already_hit;   // melee swings that already dealt damage (a swing lives ~0.2 s)

game_ui_state game_ui;
particle_system fx_system;
std::ofstream log_file;

int round_number = 0;

protected:

public:

Game();

void initilize(bool &still_in_menu, Player &player); //start up menu

bool round_on_going(int &round_count, Player &player, ai_combat_system &ai, bool &had_lost); //pure query: has someone died?
void end_round(Player &player,  ai_combat_system &ai, arrow_handler &arrow_hand); //logs, trains the ai, resets both fighters

void change_ui_state(game_ui_state new_state) {game_ui = new_state;}
game_ui_state get_game_ui_state() const {return game_ui;}

void spawn_player_attack(std::unique_ptr<base_attack> attack);
void spawn_ai_attack(std::unique_ptr<base_attack> attack);

void draw(Player& player, ai_combat_system& ai, arrow_handler& arrow,
          std::string& name, Color& color, Shape& shape, int& step);

void update(float dt, Player& player, ai_combat_system& ai, arrow_handler& arrow, bool round_on,
            std::string& name, Color& color, Shape& shape, int& step);

// access for render_ui.cpp
attack_list& player_attack_list() { return player_attacks; }
attack_list& ai_attack_list() { return ai_attacks; }
std::unordered_set<const base_attack*>& hit_once() { return already_hit; }
particle_system& fx() { return fx_system; }

const std::vector<std::unique_ptr<base_attack>>& get_player_attacks() const { return player_attacks; }
const std::vector<std::unique_ptr<base_attack>>& get_ai_attacks() const { return ai_attacks; }

};