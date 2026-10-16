#include "render_ui.h"




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
} //i know its sub optimal but i just want the issue gone... :(









void render_start_menu() {

}
void update_start_menu() {

}

void render_main_game() {

}
void update_main_game(float dt, Player &player, ai_combat_system &ai, arrow_handler &arrow, particle_system fx_system,Game game,attack_list& ai_attacks, bool round_on) {
    (void)round_on;
    fx_system.update(dt);
    game.change_current_step(1);

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
        game.end_round(player, ai, arrow);
        game.set_current_step(0);
        return;
    }

    // 5. arrows (your arrow_handler calls ai.report_arrow / add_arrow / on_arrow_pickup)
    arrow.during_round(player, ai); 
}


void render_after_round() {

}
void update_after_round() {

}

void render_end_menu() {
    
}

