#pragma once
#include <string>
#include "pch.h"
#include "player.h"          // Player, Shape
#include "ai.h"              // ai_combat_system
#include "arrow_handler.h"
#include "types.h"
#include "upgrade.h"

class Game;                  // forward declaration is enough for references

void render_start_menu();
void update_start_menu(Game& game);                     

void render_setup_menu(std::string& name, Color& color, Shape& shape, int& step);
void update_setup_menu(std::string& name, Color& color, Shape& shape, int& step, Game &game);

void render_main_game(Game& game, Player& player, ai_combat_system& ai, arrow_handler& arrow);
void update_main_game(float dt, Game& game, Player& player, ai_combat_system& ai, arrow_handler& arrow, bool round_on = true);

void render_after_round(); //this is where im gouing to put the upgrades in
void update_after_round();
void setup_upgrade_menu(std::vector<upgrade> &list);

void render_end_menu();



