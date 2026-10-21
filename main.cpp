#include "game.h"
#include "player.h"
#include "ai.h"
#include "combat.h"

#include "arrow_handler.h"

#include "pch.h"

#include <algorithm>
#include <cmath>
#include <csignal>
#include <cstdlib>
#include <ctime>
#include <memory>
#include <format>
#include <string>


extern bool debug;
extern bool g_sparring_enabled;   // in game.cpp

//for player set up
std::string player_name;
Color player_color;
Shape player_shape;
int player_step = 0;

int current_round = 1;
std::string MODEL_PATH = "modelv_1.pt";

bool in_game = true;

std::vector<upgrade> upgrade_list;
arrow_handler arrow_han;
Game game;
Player player("place_holder_name", Shape::rect, RED);
ai_combat_system ai(ai_combat_system::STATE_SIZE, ai_combat_system::ACTION_COUNT);

namespace {
constexpr float  FIXED_DT = 1.0f / 60.0f;
constexpr double TRAIN_BUDGET_MS = 12.0;            // wall-clock spent simulating per rendered frame in fast mode
constexpr int    MAX_TRAIN_STEPS_PER_FRAME = 20000;
constexpr double AUTOSAVE_SECONDS = 120.0;
constexpr double TRAIN_TIME_LIMIT_MINUTES = 5.0;    // stops after this much *fast-training* time; 0 = never
}

int main() {

    SetRandomSeed((unsigned int)time(nullptr));
    srand(static_cast<unsigned>(time(nullptr)));
    
    setup_upgrade_menu(upgrade_list); 
    srand(static_cast<unsigned>(time(nullptr)));

    player.set_position({1860.0f, 540.0f});
    ai.load_model(MODEL_PATH);                      // continue from the last run if there is one
    game.end_round(player, ai, arrow_han);          // initial reset only (nothing to score yet)
    game.change_ui_state(game_ui_state::START_MENU); //end round sets it to upgrade, make sure it stays on start

    InitWindow(1920, 1080, "Raylib 1080p Window");
    SetTargetFPS(60);

    bool is_training = false;
    float accumulator = 0.0f;
    int steps_last_frame = 0;
    double training_seconds = 0.0;
    double last_save = GetTime();

    while (!WindowShouldClose() && in_game) {
        const double frame_real = GetFrameTime();

        if (debug) {
        if (IsKeyPressed(KEY_Q)) {                  // Q = fast training on/off (sparring partner turns on with it)
            is_training = !is_training;
            g_sparring_enabled = is_training;
            SetTargetFPS(is_training ? 0 : 60);
            accumulator = 0.0f;
        }

        if (IsKeyPressed(KEY_B)) g_sparring_enabled = !g_sparring_enabled;   // B = watch the sparring bot at normal speed
    }
        // --- test input: manually spawn attacks, aimed at the mouse ---
        const Vector2 mouse_pos = GetMousePosition();
        const float aim_angle = atan2f(mouse_pos.y - player.get_pos().y, mouse_pos.x - player.get_pos().x);
        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
            player.attack_meele(game,aim_angle);
            //std::cout << "attack meele" << std::endl;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
            player.attack_ranged(game, aim_angle);
            //std::cout << "attack ranged" << std::endl;
        }

        // --- simulate ---
        if (is_training) {
            // as many fixed steps as fit in the time budget, so the window always stays responsive
            const double t0 = GetTime();
            int n = 0;
            while (n < MAX_TRAIN_STEPS_PER_FRAME) {
                game.update(FIXED_DT, player, ai, arrow_han, true,
                    player_name, player_color, player_shape, player_step, upgrade_list);
                ++n;
                if ((n & 15) == 0 && (GetTime() - t0) * 1000.0 >= TRAIN_BUDGET_MS) break;
            }
            steps_last_frame = n;
            training_seconds += frame_real;
        } else {
            accumulator += (float)std::min(frame_real, 0.1);
            steps_last_frame = 0;
            while (accumulator >= FIXED_DT) {
                game.update(FIXED_DT, player, ai, arrow_han, true, player_name,player_color,player_shape,player_step,upgrade_list);
                accumulator -= FIXED_DT;
                ++steps_last_frame;
            }
        }
        player.set_color(player_color);
        player.set_shape(player_shape);
        player.set_name(player_name);
        if (GetTime() - last_save >= AUTOSAVE_SECONDS) { ai.save_model(MODEL_PATH); last_save = GetTime(); }
        if (TRAIN_TIME_LIMIT_MINUTES > 0.0 && training_seconds >= TRAIN_TIME_LIMIT_MINUTES * 60.0) break;

        BeginDrawing();
        ClearBackground(RAYWHITE);
        game.draw(player,ai,arrow_han, player_name,player_color,player_shape,player_step, upgrade_list);
        if (debug) {
        DrawText(TextFormat("AI HP: %d   state: %s", ai.get_hp(), ai.get_state_name()), 10, 10, 20, BLACK);
        DrawText(TextFormat("eps %.3f   loss %.4f   replay %d   decisions %lld", ai.get_epsilon(),
                            ai.get_average_loss(), (int)ai.get_replay_size(), (long long)ai.get_decision_count()),
                 10, 34, 18, DARKGRAY);
        DrawText("Q: fast training    B: sparring bot    E/R: melee/arrow (aimed at mouse)", 10, 58, 18, DARKGRAY);
        }

        if (is_training) {
            DrawText(TextFormat("TRAINING (Q to stop) - %d steps this frame, %.0f s elapsed", steps_last_frame, training_seconds),
                     10, 82, 20, RED);
        } else if (g_sparring_enabled) {
            DrawText("sparring bot is controlling the player (B to stop)", 10, 82, 20, ORANGE);
        }
        EndDrawing();
    }

    CloseWindow();
    //ai.save_model(MODEL_PATH); not going to train and save rn
    return 0;
}