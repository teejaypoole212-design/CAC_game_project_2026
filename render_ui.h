#pragma once
#include <string>
#include <raylib.h>
#include "player.h"          // Player, Shape
#include "ai.h"              // ai_combat_system
#include "arrow_handler.h"
#include "types.h"

class Game;                  // forward declaration is enough for references

void render_start_menu();
void update_start_menu(Game& game);                     // <-- note the &
void render_setup_menu(std::string& name, Color& color, Shape& shape, int& step);
void update_setup_menu(std::string& name, Color& color, Shape& shape, int& step, Game &game);
void render_main_game(Game& game, Player& player, ai_combat_system& ai, arrow_handler& arrow);
void update_main_game(float dt, Game& game, Player& player, ai_combat_system& ai, arrow_handler& arrow, bool round_on = true);
void render_after_round();
void update_after_round();
void render_end_menu();