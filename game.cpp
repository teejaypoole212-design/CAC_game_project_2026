#include "game.h"
#include <raylib.h>
#include <raymath.h>

#include <cmath>

bool g_sparring_enabled = false;
sparring_bot g_sparring;

Game::Game() : game_ui(game_ui_state::START_MENU), fx_system(5000) {
    // new file name: the columns changed, so don't mix with the old training_log.csv
    log_file.open("training_log_v2.csv", std::ios::app);
    log_file.seekp(0, std::ios::end);
    if (log_file.tellp() == std::streampos(0)) {
        log_file << "round,total_reward,episode_length,final_distance,epsilon,"
                    "outcome,ai_hp,player_hp,damage_dealt,damage_taken,decisions,avg_loss,opponent\n";
    }
}

void Game::initilize(bool &still_in_menu, Player &player) {
    // Left intact per your configuration
}

// Pure query now (it used to fire reward events as a side effect, which double-counted the ending).
bool Game::round_on_going(int &round_count, Player &player, ai_combat_system &ai, bool &had_lost) {
    (void)round_count;
    const bool ai_dead = ai.get_hp() <= 0;
    const bool player_dead = !player.check_is_alive();
    if (ai_dead || player_dead) {
        if (ai_dead && !player_dead) had_lost = false;
        else if (player_dead && !ai_dead) had_lost = true;
        return false;
    }
    return true;
}

void Game::end_round(Player &player, ai_combat_system &ai, arrow_handler &arrow_hand) {
    // the very first call (from main, before the loop) is only a reset: nothing to score or log
    if (ai.get_episode_ticks() > 0) {
        round_number++;
        episode_outcome outcome = episode_outcome::TIMEOUT;
        if (ai.get_hp() <= 0) outcome = episode_outcome::AI_LOST;
        else if (!player.check_is_alive()) outcome = episode_outcome::AI_WON;
        ai.end_episode(outcome);

        const char* label = outcome == episode_outcome::AI_WON ? "win" : outcome == episode_outcome::AI_LOST ? "loss" : "timeout";
        log_file << round_number << "," << ai.get_episode_return() << "," << ai.get_episode_ticks() << ","
                 << Vector2Distance(ai.get_position(), player.get_pos()) << "," << ai.get_epsilon() << ","
                 << label << "," << ai.get_hp() << "," << player.get_health() << ","
                 << ai.get_episode_damage_dealt() << "," << ai.get_episode_damage_taken() << ","
                 << ai.get_episode_decisions() << "," << ai.get_average_loss() << ","
                 << (g_sparring_enabled ? g_sparring.name() : "human") << "\n";
        log_file.flush();

        ai.train_on_history();
    }

    arrow_hand.end_round();

    // spawn points: fixed for human play, randomised while sparring so the AI sees every geometry
    Vector2 ai_start = {60.0f, 540.0f};
    Vector2 player_start = {1860.0f, 540.0f};
    if (g_sparring_enabled) {
        ai_start = {(float)GetRandomValue(80, 500), (float)GetRandomValue(80, 1000)};
        if (GetRandomValue(0, 99) < 40) {                                   // 40%: start in a brawl
            const float ang = (float)GetRandomValue(0, 628) / 100.0f;
            const float d = (float)GetRandomValue(160, 520);
            player_start = {Clamp(ai_start.x + std::cos(ang) * d, 40.0f, 1880.0f),
                            Clamp(ai_start.y + std::sin(ang) * d, 40.0f, 1040.0f)};
        } else {
            player_start = {(float)GetRandomValue(1400, 1840), (float)GetRandomValue(80, 1000)};
        }
    }

    ai.reset_episode(ai_start);                                             // also restores the AI's hp
    player.take_damage(player.get_health() - player.get_max_health());     // negative damage = refill to max
    player.set_position(player_start);
    player_attacks.clear();                                                 // no stray swings/arrows into the new round
    ai_attacks.clear();
    already_hit.clear();
    if (g_sparring_enabled) g_sparring.begin_episode(player);
}

void Game::spawn_player_attack(std::unique_ptr<base_attack> attack) {
    player_attacks.push_back(std::move(attack));
}

void Game::spawn_ai_attack(std::unique_ptr<base_attack> attack) {
    ai_attacks.push_back(std::move(attack));
}


void Game::draw(Player& player, ai_combat_system& ai, arrow_handler& arrow, std::string& name, Color& color, Shape& shape, int& step) {
switch (this->get_game_ui_state()) {
        case game_ui_state::START_MENU: render_start_menu(); break;
        case game_ui_state::SETUP_MENU: render_setup_menu(name, color, shape, step); break;
        case game_ui_state::IN_GAME:    render_main_game(*this, player, ai, arrow); break;
        case game_ui_state::DEATH_MENU: render_end_menu(); break;
    }
}
void Game::update(float dt, Player& player, ai_combat_system& ai, arrow_handler& arrow, bool round_on, std::string& name, Color& color, Shape& shape, int& step) {
switch (this->get_game_ui_state()) {
        case game_ui_state::START_MENU: update_start_menu(*this); break;
        case game_ui_state::SETUP_MENU: update_setup_menu(name, color, shape, step, *this); break;
        case game_ui_state::IN_GAME:    update_main_game(dt, *this, player, ai, arrow, round_on); break;
        case game_ui_state::DEATH_MENU: /*update_end_menu();*/ break; //there is no update inside end (its ended :) ) 
    }
}