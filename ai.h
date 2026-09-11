#pragma once

#include "raylib.h"
#include "raymath.h"
#include <torch/torch.h>

#include <vector>
#include <deque>
#include <memory>
#include <random>
#include <string>
#include <algorithm>

#include "player.h"
#include "collision.h"
#include "combat.h"

// Forward declaration so AI knows Player exists for the frend function
class Player;

struct step_memory {
    torch::Tensor state;      
    int64_t action;           
    float reward;             
    torch::Tensor next_state; 
    bool done;                
};

enum game_state { 
    STATE_PLAYING,   
    STATE_TRAINING,  
    STATE_RESET      
}; //lots of this it dead lost funcs. i dont even remembet what the points of this was inside ai.h ?

struct combat_net_impl_type : torch::nn::Module {
    torch::nn::Linear fc1{nullptr}; 
    torch::nn::Linear fc2{nullptr}; 
    torch::nn::Linear out{nullptr}; 

    combat_net_impl_type(int64_t inputs, int64_t actions);
    torch::Tensor forward(torch::Tensor x);
};

class ai_combat_system {
private:



    float attack_cooldown_timer = 0.0f;
    float attack_cooldown_duration = 2.25f; // 24 frames of recovery at 60fps, not const anymore bc i might change it as the ai upgrades (faster attacks)


    int arrow_count = 0;

    // ADDED: Track the closest arrow's position for state vector evaluation
    Vector2 cached_closest_arrow_pos = {0.0f, 0.0f};
    bool arrow_exists_on_map = false;

    float oob_penalty_timer = 0.0f;
    std::shared_ptr<combat_net_impl_type> model;
    std::unique_ptr<torch::optim::Adam> optimizer;
    std::deque<step_memory> replay_memory;
    std::shared_ptr<combat_net_impl_type> target_model;

    const size_t max_memory_capacity = 50000; 
    float epsilon = 0.50f;       
    const float epsilon_min = 0.05f;   
    const float epsilon_decay = 0.01; 
    const float gamma = 0.99f;         
    const int batch_size = 64;         

    // Anti-lazy mechanics trackers
    std::deque<int64_t> recent_actions;
    const size_t max_action_history = 5;
    float last_player_distance = 0.0f;

    // Frame-stacking buffer: holds the last `stack_size` raw per-frame feature vectors
    mutable std::deque<std::vector<float>> frame_history;

    float accumulated_event_reward = 0.0f;
    Vector2 ai_position = {60.0f,540.0f};
    Vector2 last_ai_position = {0,0};
    int frames_standing_still = 0;

    // Added to give the input tensor actual health values to normalize
    int ai_hp = 100;
    int ai_max_hp = 100;

    collision_mask hitbox;
    bool had_hit_this_frame = false; //same inside reward and update checks
    bool had_attacked_this_frame = false; //call when it attack to let it know it did then reset it when reward is given out
    int state_elapsed_frames = 0;

public:
    ai_combat_system(int64_t input_size, int64_t action_size);

    static constexpr int64_t raw_state_size = 12;  // size of ONE frame's features
    static constexpr int64_t stack_size = 4;        // how many frames we stack together


    // Grant access to the shared input pipeline function
    torch::Tensor generate_ai_state_tensor(const Player& player, int current_step, bool commit_to_history = true);

    void on_ai_damage();
    void on_ai_death();
    void on_ai_win();

    void draw(); 

    void save_model(const std::string& path);
    void load_model(const std::string& path);

    void reset_hp() {ai_hp = ai_max_hp;}
    void take_damage(int amount) {ai_hp -= amount;}

    void set_epsilon(float new_eps) {epsilon = new_eps;}
    void reset_episode(Vector2 ai_start_pos);
    int64_t choose_action(torch::Tensor current_state);
    void update_ai_movement(Vector2& current_pos, int64_t action, float speed);

    float process_and_record_frame(torch::Tensor state, int64_t action, 
                                   torch::Tensor next_state, Vector2 current_ai_pos, 
                                   Vector2 player_pos, bool is_done);
                                
    void train_on_history();


    //AI STATES 
    void move_tords_player(Player &player, int step_duration);
    void run_away_from_player(Player &player, int step_duration);
    void go_to_pickup_arrow();
    void move_behind_cover();
    void bait_player_attack(Player &player, int step_duration); //move away and tords player semi quick to trick player
    void strafe_around_player(Player &player, int step_duration); //using the player as center point walk a semi circle around player (maybe can bait mid way through)
    void zig_zag_to_player(Player &player, int step_duration);
    void fake_run_away(Player &player, int step_duration); //run away a little and then go in for the attack
    //attacks are hard coded inside case inside game.cpp (need to add raycast check to them though for line of sight)

    void on_arrow_pickup() { accumulated_event_reward += 30.0f; } // Strong spike for reaching objective

    void update_tracked_arrow(Vector2 arrow_pos, bool exists) {
        cached_closest_arrow_pos = arrow_pos;
        arrow_exists_on_map = exists;
    }

    void tick_cooldowns(float dt) {
        if (attack_cooldown_timer > 0.0f) attack_cooldown_timer -= dt;
    }
    bool can_attack() const { return attack_cooldown_timer <= 0.0f; }
    void reset_attack_cooldown() { attack_cooldown_timer = attack_cooldown_duration; }

    // Setters/Getters for your game loop to update AI stats
    Vector2 get_position() const { return ai_position; }
    Vector2 get_last_position() const { return last_ai_position; }
    void change_position_x(float change) { ai_position.x += change; hitbox.set_position(ai_position); } //just so pos gets updated for self and hit box
    void change_position_y(float change) { ai_position.y += change; hitbox.set_position(ai_position); }
    void set_hp(int hp) { ai_hp = hp; }
    int get_hp() const { return ai_hp; }
    int get_max_hp() const { return ai_max_hp; }
    float get_epsilon() const { return epsilon; }
    const collision_mask& get_hitbox() const { return hitbox; }
    int get_arrow_count() const {return arrow_count;}
    void add_arrow(int amount) {arrow_count += amount;} //to remove just add a neg :(
    float get_attack_cooldown_timer() const {return attack_cooldown_timer;}
    const float get_attack_cooldown_duration() const {return attack_cooldown_duration;}
    void set_had_hit_this_frame(bool change) {had_hit_this_frame = change;}
    bool get_had_hit_this_frame() const {return had_hit_this_frame;}
    void set_attacked_this_frame(bool change) {had_attacked_this_frame = change;}
    bool get_had_attacked_this_frame() const {return had_attacked_this_frame;}
    void reset_behavior_timer() { state_elapsed_frames = 0; }
    bool has_line_of_sight(Vector2 target_pos) const;
    bool is_in_melee_range(Vector2 target_pos) const;
    bool is_in_ranged_range(Vector2 target_pos) const;
    bool can_perform_melee(Vector2 target_pos) const;
    bool can_perform_ranged(Vector2 target_pos) const;
    bool has_good_reason_to_flee() const;
    

};
