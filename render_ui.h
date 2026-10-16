#pragma once

#include "player.h"
#include "arrow_handler.h"
#include "ai.h"
#include "particle.h"
#include "game.h"

void render_start_menu(); 
void update_start_menu();

void render_main_game();
void update_main_game(float dt, Player &player, ai_combat_system &ai, arrow_handler &arrow, particle_system fx_system,Game game,attack_list& ai_attacks, bool round_on);

void render_after_round();
void update_after_round();

void render_end_menu();

