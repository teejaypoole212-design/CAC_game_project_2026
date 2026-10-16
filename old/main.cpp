#include "game.h"
#include "player.h"
#include "ai.h"
#include "combat.h"

#include "arrow_handler.h"


#include <raylib.h>
#include <raymath.h>

#include <cstdlib>
#include <ctime>
#include <memory>

#include <atomic>
#include <thread>
#include <chrono>
#include <csignal>


std::atomic<bool> g_time_limit_reached{false};
volatile sig_atomic_t g_shutdown_requested = 0;

void handle_sigint(int) { g_shutdown_requested = 1; }

void run_training_timer(std::chrono::minutes duration) {
    std::this_thread::sleep_for(duration);
    g_time_limit_reached.store(true, std::memory_order_relaxed);
}

arrow_handler arrow_han;
Game game;
Player player("place_holder_name", Shape::rect, RED);
ai_combat_system ai(ai_combat_system::raw_state_size * ai_combat_system::stack_size, 10);

bool is_training = false;              // debug flag - toggle with F1 to fast-forward training
constexpr int TRAINING_SUBSTEPS = 500; // game.update() calls per rendered frame while training

int main() {

        std::signal(SIGINT, handle_sigint);

    std::thread timer_thread(run_training_timer, std::chrono::minutes(5));
    timer_thread.detach(); // fine to detach: it owns nothing but a sleep + flag write

    player.set_position({1860.0f, 540.0f}); 
    srand(static_cast<unsigned>(time(nullptr)));
    game.end_round(player, ai, arrow_han);
    const int screenWidth = 1920;
    const int screenHeight = 1080;

    int round_number = 0;
    bool had_lost = false;

    InitWindow(screenWidth, screenHeight, "Raylib 1080p Window");

    SetTargetFPS(60);

    while (!WindowShouldClose() && !g_shutdown_requested && !g_time_limit_reached.load(std::memory_order_relaxed) ) {

        if (IsKeyPressed(KEY_Q)) {
            is_training = !is_training;
            // uncap FPS while training so the substep loop isn't throttled
            // to 60 real-world calls/sec by vsync/frame limiting
            SetTargetFPS(is_training ? 0 : 60);
        }

        float dt = GetFrameTime(); // stable value, reused across every substep this frame

        // --- Test input: manually spawn attacks, aimed at the mouse ---
        Vector2 mouse_pos = GetMousePosition();
        Vector2 to_mouse = { mouse_pos.x - player.get_pos().x, mouse_pos.y - player.get_pos().y };
        float aim_angle = atan2f(to_mouse.y, to_mouse.x);

        if (IsKeyPressed(KEY_E)) {
            auto swing = std::make_unique<meele_attack>(10, 5, 60, YELLOW, DARKBROWN);
            swing->initilize(player.get_pos(), aim_angle);
            game.spawn_player_attack(std::move(swing));
        }
        if (IsKeyPressed(KEY_R)) {
            auto arrow = std::make_unique<ranged_attack>(10, 400, SKYBLUE);
            arrow->initilize(player.get_pos(), aim_angle);
            game.spawn_player_attack(std::move(arrow));
        }

        int steps_this_frame = is_training ? TRAINING_SUBSTEPS : 1;
        for (int step = 0; step < steps_this_frame; ++step) {
            bool round_going = game.round_on_going(round_number,player, ai, had_lost);
            game.update(dt, player, ai, arrow_han, round_going);
        }

        BeginDrawing();
        ClearBackground(RAYWHITE);

        game.draw(player, ai);

        DrawText(TextFormat("AI HP: %d", ai.get_hp()), 10, 10, 20, BLACK);
        if (is_training) {
            DrawText(TextFormat("TRAINING MODE (F1 to toggle) - %d steps/frame", TRAINING_SUBSTEPS), 10, 40, 20, RED);
        }

        EndDrawing();
    }

    CloseWindow();
    ai.save_model("model.pt");
    return 0;
}