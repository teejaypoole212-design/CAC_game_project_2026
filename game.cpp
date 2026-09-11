#include "game.h"
#include <raylib.h>

// Keep this single global tracker if shared with other systems
int current_step = 0; 

// line 141 — the new one you added for logging
Game::Game() : game_ui(game_ui_state::START_MENU), fx_system(5000) {
    log_file.open("training_log.csv", std::ios::app);
    log_file << "round,total_reward,episode_length,final_distance,epsilon\n";
}

void Game::initilize(bool &still_in_menu, Player &player) {
    // Left intact per your configuration
}


void Game::check_and_handle_auto_reset(Player &player, ai_combat_system &ai, int &current_step) {
    bool had_lost = false;
    int dummy_round = 0;

   
    if (!this->round_on_going(dummy_round, player, ai, had_lost)) {
        
        
        this->end_round(player, ai); 
        
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


bool Game::round_on_going(int &round_count, Player &player, ai_combat_system &ai, bool &had_lost) {
    if (ai.get_hp() <= 0 || !player.check_is_alive()) {
        if (ai.get_hp() <= 0 && player.check_is_alive()) {
            ai.on_ai_death(); 
            had_lost = false; 
        } 
        else if (!player.check_is_alive() && ai.get_hp() > 0) {
            ai.on_ai_win();   
            had_lost = true;  
        }
        return false; 
    }
    return true; 
}


void Game::update(float dt, Player &player, ai_combat_system &ai) {
    fx_system.update(dt);
    current_step++;

    ai.tick_cooldowns(dt);


if (ai.get_hitbox().check_collision(player.get_hitbox())) {
    end_round(player,ai); //debug for now to allow for automated ai training
}




    Vector2 live_ai_pos = ai.get_position(); 
    Vector2 old_ai_pos = live_ai_pos; 

    torch::Tensor current_state;
    {
        torch::NoGradGuard no_grad;
        current_state = ai.generate_ai_state_tensor(player, current_step);
    }

    int64_t chosen_action = ai.choose_action(current_state);

// action -> behavior mapping. keeping speed-based melee/ranged untouched,
// but the movement actions now dispatch to the high-level AI states
// instead of raw single-axis moves.
    constexpr int BEHAVIOR_STEP_DURATION = 45; // ~0.75s at 60fps, tune to taste

    static int64_t previous_action = -1; // tracks the last action picked, so
                                          // phase-based states don't get a
                                          // corrupted timer when the action changes
    if (chosen_action != previous_action) {
        ai.reset_behavior_timer();
        previous_action = chosen_action;
    }

    switch (chosen_action) {
        case 0: ai.move_tords_player(player, BEHAVIOR_STEP_DURATION);     break;
        case 1: ai.run_away_from_player(player, BEHAVIOR_STEP_DURATION);  break;
        case 2: ai.go_to_pickup_arrow();                                  break;
        case 3: ai.move_behind_cover();                                   break;
        case 4: ai.bait_player_attack(player, BEHAVIOR_STEP_DURATION);    break;
        case 5: ai.strafe_around_player(player, BEHAVIOR_STEP_DURATION);  break;
        case 6: ai.zig_zag_to_player(player, BEHAVIOR_STEP_DURATION);     break;
        case 7: ai.fake_run_away(player, BEHAVIOR_STEP_DURATION);         break;

        case 8: { // MELEE SWING
            if (ai.can_attack()) {
                ai.reset_attack_cooldown();
                float facing_angle = atan2f(player.get_pos().y - live_ai_pos.y, player.get_pos().x - live_ai_pos.x);
                auto melee = std::make_unique<meele_attack>(15.0, 0.0, 60.0, MAROON, RED);
                melee->initilize(live_ai_pos, facing_angle);
                this->spawn_ai_attack(std::move(melee));
            }
            break;
        }
        case 9: { // RANGED PROJECTILE
            if (ai.get_arrow_count() > 0 && ai.can_attack()) {
                ai.reset_attack_cooldown();
                float facing_angle = atan2f(player.get_pos().y - live_ai_pos.y, player.get_pos().x - live_ai_pos.x);
                auto arrow = std::make_unique<ranged_attack>(10.0, 450.0, PURPLE);
                arrow->initilize(live_ai_pos, facing_angle);
                this->spawn_ai_attack(std::move(arrow));
            }
            break;
        }
    }

    // movement states (0,1,4,5,6,7) mutate ai's internal position directly via
    // change_position_x/y, not live_ai_pos - resync so the rest of this frame
    // (state tensor, collision, attack spawn positions) stays consistent
    if (chosen_action >= 0 && chosen_action <= 7) {
        live_ai_pos = ai.get_position();
    }

    player.update(dt); 
// temp note out for now plan to revamp this system anyways
    //ai.change_position_x(live_ai_pos.x - old_ai_pos.x);
    //ai.change_position_y(live_ai_pos.y - old_ai_pos.y);

    update_attacks(dt, player.get_pos(), ai.get_position());

    bool is_done = (ai.get_hp() <= 0 || !player.check_is_alive());

    // Damage processing blocks remain the same...
    for (auto& atk : player_attacks) { /* ... */ }
    for (auto& atk : ai_attacks) { /* ... */ }

    torch::Tensor next_state;
    {
        torch::NoGradGuard no_grad;
        next_state = ai.generate_ai_state_tensor(player, current_step);
    }

    float frame_reward = ai.process_and_record_frame(current_state, chosen_action, next_state, live_ai_pos, player.get_pos(), is_done);
    episode_reward_total += frame_reward;
    episode_length++;

    is_done = (ai.get_hp() <= 0 || !player.check_is_alive());

    if (is_done) {
        current_step = 0; 
        this->end_round(player, ai);
    }
    check_and_handle_auto_reset(player, ai, current_step);
}


void Game::end_round(Player &player, ai_combat_system &ai) {
    
    round_number++;
    float final_dist = Vector2Distance(ai.get_position(), player.get_pos());
    log_file << round_number << "," << episode_reward_total << "," << episode_length << "," 
              << final_dist << "," << ai.get_epsilon() << "\n";
    log_file.flush();

    episode_reward_total = 0.0f;
    episode_length = 0;


    ai.train_on_history();

    float x = 60.0f;
    float y = 540.0f;
    
    Vector2 ai_start_pos = {x, y};

    ai.reset_hp();
    ai.reset_episode(ai_start_pos);

    // Reset player back to standard configurations
    //player.set_position({1860.0f, 540.0f}); for now let player stay...
    float newposx = GetRandomValue(100,1760);
    float newposy = GetRandomValue(50,490);
    player.set_position({newposx,newposy});
    //player.update(); 
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