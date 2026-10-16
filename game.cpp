#include "game.h"
#include <raylib.h>
#include <raymath.h>
#include "arrow_handler.h"


#include <algorithm>
#include <cmath>
#include <unordered_set>

// Keep this single global tracker if shared with other systems

// main.cpp flips this (Q = fast training, B = toggle). When on, a scripted sparring partner
// pilots the Player so the AI has an opponent that fights back.
bool g_sparring_enabled = false;
static sparring_bot g_sparring;

namespace {

// A melee swing stays alive for ~0.2 s: make sure it only damages once.
std::unordered_set<const base_attack*> g_already_hit;

// Both directions of combat damage in one place. Arrows are consumed on hit.
void resolve_damage(attack_list& player_attacks, attack_list& ai_attacks, Player& player, ai_combat_system& ai) {
    for (auto& up : player_attacks) {                               // player -> AI
        base_attack* a = up.get();
        if (!a->is_Alive() || g_already_hit.count(a)) continue;
        if (!a->get_hitbox().check_collision(ai.get_hitbox())) continue;
        ai.take_damage((int)a->get_damage());                       // the AI learns from this automatically
        if (dynamic_cast<ranged_attack*>(a)) a->kill(); else g_already_hit.insert(a);
    }
    for (auto& up : ai_attacks) {                                   // AI -> player
        base_attack* a = up.get();
        if (!a->is_Alive() || g_already_hit.count(a)) continue;
        if (!a->get_hitbox().check_collision(player.get_hitbox())) continue;
        const int dmg = (int)a->get_damage();
        const bool melee = dynamic_cast<meele_attack*>(a) != nullptr;
        player.take_damage(dmg);
        ai.on_hit_player(dmg, melee);
        if (melee) g_already_hit.insert(a); else a->kill();
    }
    // forget swings that no longer exist (heap addresses get reused)
    for (auto it = g_already_hit.begin(); it != g_already_hit.end();) {
        bool alive = false;
        for (auto& up : player_attacks) if (up.get() == *it) { alive = true; break; }
        if (!alive) for (auto& up : ai_attacks) if (up.get() == *it) { alive = true; break; }
        it = alive ? std::next(it) : g_already_hit.erase(it);
    }
}

} // namespace

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

void Game::check_and_handle_auto_reset(Player &player, ai_combat_system &ai, arrow_handler &arrow, int &current_step) {
    bool had_lost = false;
    int dummy_round = 0;
    if (!this->round_on_going(dummy_round, player, ai, had_lost)) {
        this->end_round(player, ai, arrow);
        current_step = 0;
    }
}

void Game::update_attacks(float dt, Vector2 player_pos, Vector2 ai_pos) {
    for (auto& a : player_attacks) if (a->is_Alive()) a->update(dt, player_pos); // follows PLAYER
    for (auto& a : ai_attacks)     if (a->is_Alive()) a->update(dt, ai_pos);     // follows AI

    auto is_dead = [](const std::unique_ptr<base_attack>& a) { return !a->is_Alive(); };
    player_attacks.erase(std::remove_if(player_attacks.begin(), player_attacks.end(), is_dead), player_attacks.end());
    ai_attacks.erase(std::remove_if(ai_attacks.begin(), ai_attacks.end(), is_dead), ai_attacks.end());
}

void Game::draw_attacks() {
    for (auto& a : player_attacks) if (a->is_Alive()) a->draw(a->get_pos());
    for (auto& a : ai_attacks)     if (a->is_Alive()) a->draw(a->get_pos());
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

void Game::update(float dt, Player &player, ai_combat_system &ai, arrow_handler &arrow, bool round_on) {
    (void)round_on;
    fx_system.update(dt);
    current_step++;

    // 1. optional scripted opponent (training)
    static bool bot_was_on = false;
    if (g_sparring_enabled && !bot_was_on) g_sparring.begin_episode(player);
    bot_was_on = g_sparring_enabled;
    if (g_sparring_enabled) {
        g_sparring.update(dt, player, ai, ai_attacks,
                          [this](std::unique_ptr<base_attack> a) { spawn_player_attack(std::move(a)); });
    }

    // 2. the AI: perceive -> (re)decide -> run its state -> move -> attack
    ai.update(dt, player, player_attacks,
              [this](std::unique_ptr<base_attack> a) { spawn_ai_attack(std::move(a)); });

    // 3. world
    player.update(dt);
    update_attacks(dt, player.get_pos(), ai.get_position());
    resolve_damage(player_attacks, ai_attacks, player, ai);

    // 4. round over?
    if (ai.get_hp() <= 0 || !player.check_is_alive() || ai.episode_timed_out()) {
        this->end_round(player, ai, arrow);
        current_step = 0;
        return;
    }

    // 5. arrows (your arrow_handler calls ai.report_arrow / add_arrow / on_arrow_pickup)
    arrow.during_round(player, ai);
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
    g_already_hit.clear();
    if (g_sparring_enabled) g_sparring.begin_episode(player);
}

void Game::draw(Player &player, ai_combat_system &ai) {
    player.draw();
    ai.draw();
    fx_system.draw();
    draw_attacks();
}

void Game::spawn_player_attack(std::unique_ptr<base_attack> attack) {
    player_attacks.push_back(std::move(attack));
}

void Game::spawn_ai_attack(std::unique_ptr<base_attack> attack) {
    ai_attacks.push_back(std::move(attack));
}