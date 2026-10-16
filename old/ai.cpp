#include "ai.h"
#include "raymath.h" // FIX: Restores FloatEquals declaration scope
#include <algorithm>
#include <random>
#include <numeric>
#include <cmath>

// Underlying layer components configurations
combat_net_impl_type::combat_net_impl_type(int64_t inputs, int64_t actions) {
    fc1 = register_module("fc1", torch::nn::Linear(inputs, 128));
    fc2 = register_module("fc2", torch::nn::Linear(128, 128));
    out = register_module("out", torch::nn::Linear(128, actions));
}

torch::Tensor combat_net_impl_type::forward(torch::Tensor x) {
    x = torch::relu(fc1->forward(x)); 
    x = torch::relu(fc2->forward(x));
    return out->forward(x);           
}

// Master Wrapper Constructor
ai_combat_system::ai_combat_system(int64_t input_size, int64_t action_size) {
    model = std::make_shared<combat_net_impl_type>(input_size, action_size);
    target_model = std::make_shared<combat_net_impl_type>(input_size, action_size); // <-- ADD
    
    // Sync their weights initially
    torch::NoGradGuard no_grad;
    auto params = model->parameters();
    auto target_params = target_model->parameters();
    for (size_t i = 0; i < params.size(); ++i) {
        target_params[i].copy_(params[i]);
    }

    optimizer = std::make_unique<torch::optim::Adam>(model->parameters(), torch::optim::AdamOptions(0.0005)); // Lower LR for stability
    last_player_distance = 0.0f; 
}










#include "ai.h"
#include <cmath>
#include <cstdlib>

// defined by you elsewhere (main.cpp/game.cpp) as a plain global -
// must not be `static` or inside an anonymous namespace or extern can't see it
bool debug = true;

// ---------------------------------------------------------------------
// tuning constants for the movement states below. pull these into the
// class as members if you want them adjustable at runtime (e.g. scaling
// with difficulty), but plain constants are fine to start.
// ---------------------------------------------------------------------
namespace {
    constexpr float AI_BASE_SPEED     = 180.0f; // px/sec, normal walk speed
    constexpr float AI_BAIT_SPEED     = 260.0f; // px/sec, burst speed for bait/fake attacks
    constexpr float MIN_APPROACH_DIST = 45.0f;  // don't walk on top of the player
    constexpr float STRAFE_RADIUS     = 160.0f; // orbit distance for strafing
    constexpr float STRAFE_ANGULAR_SPEED = 1.4f; // radians/sec
    constexpr float ZIGZAG_AMPLITUDE  = 90.0f;  // px, sideways sway strength
    constexpr float ZIGZAG_FREQUENCY  = 4.0f;   // sway oscillations, tune to taste
    constexpr float ARROW_ARRIVE_DIST = 5.0f;

    // natural-retreat tuning: slow modulation so backing off reads as a
    // person hesitating/surging, not a jitter or a perfect straight line
    constexpr float RETREAT_SPEED_WAVE_FREQ = 1.4f;  // rad/sec, how fast the surge/hesitate cycles
    constexpr float RETREAT_SWAY_FREQ       = 0.9f;  // rad/sec, slower than the speed wave so they don't sync up
    constexpr float RETREAT_SWAY_AMPLITUDE  = 55.0f; // px/sec, side-to-side weight shift strength

    // chase smoothing + dive tuning
    constexpr float CHASE_EASE_ZONE   = 120.0f; // px from stop distance where speed starts easing down
    constexpr float CHASE_EASE_FLOOR  = 0.35f;  // never eases below this fraction of base speed - avoids a crawl-to-stop feel
    constexpr float DIVE_CHANCE       = 0.25f;  // per chase cycle, chance to commit to a dive instead of a normal approach
    constexpr float AI_DIVE_SPEED     = 320.0f; // px/sec, committed lunge speed - noticeably faster than base
    constexpr float DIVE_CLOSE_DIST   = 20.0f;  // non-debug dive stop distance - close enough to read as "in your face" without full overlap
}

// normalized direction from `from` to `to`. returns {0,0} if they're
// basically on top of each other so we don't NaN out on normalize.
static Vector2 dir_to(Vector2 from, Vector2 to) {
    Vector2 delta = Vector2Subtract(to, from);
    if (Vector2LengthSqr(delta) < 0.0001f) return {0.0f, 0.0f};
    return Vector2Normalize(delta);
}

// Returns a velocity vector (px/sec) for a natural-looking retreat along
// `retreat_dir` (must already be normalized). Speed surges and eases
// between 75-100% of base_speed on a slow sine wave - it never drops to
// zero or reverses, so movement always nets backward, it just doesn't move
// at a robotic constant rate. A slower, out-of-phase perpendicular sway
// layers in a bit of side-to-side weight shift, like someone backing off
// while still squared up to a threat rather than sliding in a straight line.
static Vector2 natural_retreat_step(Vector2 retreat_dir, float base_speed, float phase_time) {
    Vector2 perp = { -retreat_dir.y, retreat_dir.x };

    float speed_wave = 0.75f + 0.25f * sinf(phase_time * RETREAT_SPEED_WAVE_FREQ);
    float current_speed = base_speed * speed_wave;

    float sway = sinf(phase_time * RETREAT_SWAY_FREQ + 1.7f) * RETREAT_SWAY_AMPLITUDE; // phase offset so sway and surge don't line up

    return Vector2Add(Vector2Scale(retreat_dir, current_speed),
                       Vector2Scale(perp, sway));
}

void ai_combat_system::move_tords_player(Player &player, int step_duration) {
    state_elapsed_frames++;

    Vector2 player_pos = player.get_pos();
    float dist = Vector2Distance(ai_position, player_pos);
    if (dist <= MIN_APPROACH_DIST) return; // already in close enough

    Vector2 dir = dir_to(ai_position, player_pos);
    float dt = GetFrameTime();

    change_position_x(dir.x * AI_BASE_SPEED * dt);
    change_position_y(dir.y * AI_BASE_SPEED * dt);
}

void ai_combat_system::run_away_from_player(Player &player, int step_duration) {
    state_elapsed_frames++;
    float dt = GetFrameTime();

    Vector2 player_pos = player.get_pos();
    Vector2 dir = dir_to(player_pos, ai_position); // pointing away from player
    float phase_time = (float)state_elapsed_frames * dt;

    Vector2 velocity = natural_retreat_step(dir, AI_BASE_SPEED, phase_time);
    change_position_x(velocity.x * dt);
    change_position_y(velocity.y * dt);
}

void ai_combat_system::go_to_pickup_arrow() {
    state_elapsed_frames++;

    if (!arrow_exists_on_map) return; // nothing to go grab right now

    float dist = Vector2Distance(ai_position, cached_closest_arrow_pos);
    if (dist < ARROW_ARRIVE_DIST) return; // close enough, let the pickup trigger do its thing

    Vector2 dir = dir_to(ai_position, cached_closest_arrow_pos);
    float dt = GetFrameTime();

    change_position_x(dir.x * AI_BASE_SPEED * dt);
    change_position_y(dir.y * AI_BASE_SPEED * dt);
}

void ai_combat_system::move_behind_cover() {
    state_elapsed_frames++;

    // TODO: needs an actual source of cover-point data - e.g. a
    // std::vector<Vector2> of cover spots baked into the map/level, or a
    // get_nearest_cover_point(ai_position) helper off your collision system.
    // Nothing in ai_combat_system or Player currently exposes that.
    //
    // Once you have it, this is the exact same pattern as go_to_pickup_arrow():
    // find nearest cover point -> dir_to() it -> move -> stop once arrived.
}

// NOTE: signature changed from the header stub - needs Player to know
// which direction "away" and "toward" actually are. update ai.h + call
// sites in game.cpp to match: bait_player_attack(Player &player, int step_duration);
void ai_combat_system::bait_player_attack(Player &player, int step_duration) {
    state_elapsed_frames++;
    float dt = GetFrameTime();
    Vector2 player_pos = player.get_pos();

    int half = step_duration / 2;

    if (state_elapsed_frames <= half) {
        // phase 1: back off to sell the "retreating" read - natural surge/sway
        // sells it as a real hesitation, not a scripted step-back
        Vector2 dir = dir_to(player_pos, ai_position);
        float phase_time = (float)state_elapsed_frames * dt;
        Vector2 velocity = natural_retreat_step(dir, AI_BASE_SPEED, phase_time);
        change_position_x(velocity.x * dt);
        change_position_y(velocity.y * dt);
    } else {
        // phase 2: snap back in faster than the retreat, baiting a whiffed swing.
        // deliberately straight-line and fast here - this part SHOULD read as
        // sudden and committed, contrasting against the hesitant retreat
        Vector2 dir = dir_to(ai_position, player_pos);
        change_position_x(dir.x * AI_BAIT_SPEED * dt);
        change_position_y(dir.y * AI_BAIT_SPEED * dt);
    }
}

void ai_combat_system::strafe_around_player(Player &player, int step_duration) {
    state_elapsed_frames++;
    float dt = GetFrameTime();
    Vector2 player_pos = player.get_pos();

    Vector2 offset = Vector2Subtract(ai_position, player_pos);
    if (Vector2Length(offset) < 1.0f) offset = {STRAFE_RADIUS, 0.0f}; // avoid NaN if standing on player

    // flip direction partway through the duration so it can bait mid-strafe
    int half = step_duration / 2;
    float sign = (state_elapsed_frames <= half) ? 1.0f : -1.0f;
    float angle_step = STRAFE_ANGULAR_SPEED * dt * sign;

    Vector2 new_offset = Vector2Rotate(offset, angle_step);
    new_offset = Vector2Scale(Vector2Normalize(new_offset), STRAFE_RADIUS); // hold a fixed orbit radius

    Vector2 target_pos = Vector2Add(player_pos, new_offset);
    Vector2 move_dir = dir_to(ai_position, target_pos);

    change_position_x(move_dir.x * AI_BASE_SPEED * dt);
    change_position_y(move_dir.y * AI_BASE_SPEED * dt);
}

void ai_combat_system::zig_zag_to_player(Player &player, int step_duration) {
    state_elapsed_frames++;
    float dt = GetFrameTime();
    Vector2 player_pos = player.get_pos();

    Vector2 forward = dir_to(ai_position, player_pos);
    Vector2 perpendicular = {-forward.y, forward.x}; // 90 degrees off the approach line

    float t = (float)state_elapsed_frames * dt;
    float sway = sinf(t * ZIGZAG_FREQUENCY) * ZIGZAG_AMPLITUDE * dt;

    Vector2 move = Vector2Add(Vector2Scale(forward, AI_BASE_SPEED * dt),
                               Vector2Scale(perpendicular, sway));

    change_position_x(move.x);
    change_position_y(move.y);
}

// NOTE: signature changed - see bait_player_attack note above.
void ai_combat_system::fake_run_away(Player &player, int step_duration) {
    state_elapsed_frames++;
    float dt = GetFrameTime();
    Vector2 player_pos = player.get_pos();

    int flee_phase_len = step_duration / 3; // flee for a third of the duration, then commit

    if (state_elapsed_frames <= flee_phase_len) {
        Vector2 dir = dir_to(player_pos, ai_position);
        float phase_time = (float)state_elapsed_frames * dt;
        Vector2 velocity = natural_retreat_step(dir, AI_BASE_SPEED, phase_time);
        change_position_x(velocity.x * dt);
        change_position_y(velocity.y * dt);
    } else {
        Vector2 dir = dir_to(ai_position, player_pos);
        change_position_x(dir.x * AI_BAIT_SPEED * dt);
        change_position_y(dir.y * AI_BAIT_SPEED * dt);
    }
}

// ---------------------------------------------------------------------
// combat gating + tactical justification helpers
// ---------------------------------------------------------------------
namespace {
    constexpr float MELEE_EFFECTIVE_RANGE = 80.0f;   // roughly matches meele_attack's 60px reach + some approach slop
    constexpr float RANGED_MIN_RANGE      = 40.0f;   // don't loose an arrow point-blank, melee wins that trade
    constexpr float RANGED_MAX_RANGE      = 600.0f;  // past this the shot's low percentage, don't bother
    constexpr float LOW_HP_FLEE_THRESHOLD = 0.35f;   // at/below 35% hp counts as a real reason to disengage
}

bool ai_combat_system::has_line_of_sight(Vector2 target_pos) const {
    // finalized, not a stub: collision.h only defines entity-vs-entity
    // hitbox overlap (circle/rect/triangle), there's no wall/obstacle
    // geometry or raycast anywhere in this codebase - the arena is open,
    // bounded only by the 1920x1080 out-of-bounds check already handled
    // elsewhere. nothing can occlude the player, so this always holds.
    // if you ever add level obstacles, this is the one place to update.
    (void)target_pos; // unused until obstacles exist
    return true;
}

bool ai_combat_system::is_in_melee_range(Vector2 target_pos) const {
    return Vector2Distance(ai_position, target_pos) <= MELEE_EFFECTIVE_RANGE;
}

bool ai_combat_system::is_in_ranged_range(Vector2 target_pos) const {
    float dist = Vector2Distance(ai_position, target_pos);
    return dist >= RANGED_MIN_RANGE && dist <= RANGED_MAX_RANGE;
}

bool ai_combat_system::can_perform_melee(Vector2 target_pos) const {
    return can_attack() && is_in_melee_range(target_pos) && has_line_of_sight(target_pos);
}

bool ai_combat_system::can_perform_ranged(Vector2 target_pos) const {
    return can_attack() && arrow_count > 0 && is_in_ranged_range(target_pos) && has_line_of_sight(target_pos);
}

// "does fleeing right now make tactical sense" - used by the reward
// function to tell genuine self-preservation apart from a cowardly agent
// that just avoids the player for no reason. deliberate feints
// (bait_player_attack / fake_run_away) are exempt from this check since
// baiting is a legitimate tactic regardless of current hp.
bool ai_combat_system::has_good_reason_to_flee() const {
    float hp_ratio = static_cast<float>(ai_hp) / static_cast<float>(ai_max_hp);
    return hp_ratio <= LOW_HP_FLEE_THRESHOLD;
}




void ai_combat_system::on_ai_damage() {
    accumulated_event_reward -= 15.0f; 
}

void ai_combat_system::on_ai_death() {
    accumulated_event_reward -= 50.0f; 
}

void ai_combat_system::on_ai_win() {
    accumulated_event_reward += 50.0f; 
}

void ai_combat_system::reset_episode(Vector2 ai_start_pos) {
    accumulated_event_reward = 0.0f;
    ai_position = ai_start_pos;
    last_ai_position = ai_start_pos;
    frames_standing_still = 0;
    oob_penalty_timer = 0.0f;
    frame_history.clear(); 
}

int64_t ai_combat_system::choose_action(torch::Tensor current_state) {
    float roll = static_cast<float>(rand()) / static_cast<float>(RAND_MAX);
    
    if (roll < epsilon) {
        return rand() % 10;
    } else {
        torch::NoGradGuard no_grad; 
        // FIX: The shared model instance safely triggers forward via arrow notation
        torch::Tensor q_values = model->forward(current_state);
        return q_values.argmax(1).item<int64_t>(); 
    }
}

// std::deque<int64_t> recent_actions;
// const size_t max_action_history = 5;






//to determine and give out pos and neg reward
float ai_combat_system::process_and_record_frame(torch::Tensor state, int64_t action, 
                                                   torch::Tensor next_state, Vector2 current_ai_pos, 
                                                   Vector2 player_pos, bool is_done) {
    // 1. Gentle Base Penalty
    float frame_reward = -0.01f; 

    bool is_out_of_bounds = (current_ai_pos.x < 0.0f || current_ai_pos.x > 1920.0f || 
                              current_ai_pos.y < 0.0f || current_ai_pos.y > 1080.0f);

    if (is_out_of_bounds) {
        oob_penalty_timer += GetFrameTime(); 
        if (oob_penalty_timer >= 1.0f) {
            this->take_damage(100);
            this->on_ai_damage();
            this->on_ai_damage();
            oob_penalty_timer = 0.0f; 
        }
    } else {
        oob_penalty_timer = 0.0f;
    }
    frame_reward += accumulated_event_reward;
    accumulated_event_reward = 0.0f; 

    // 2. Balanced Standstill Check
    if (FloatEquals(current_ai_pos.x, last_ai_position.x) && 
        FloatEquals(current_ai_pos.y, last_ai_position.y)) {
        frames_standing_still++;
    } else {
        frames_standing_still = 0; 
    }

    if (frames_standing_still > 10) {
        float standstill_penalty = static_cast<float>(frames_standing_still) * 0.005f;
        if (standstill_penalty > 0.25f) standstill_penalty = 0.25f;
        frame_reward -= standstill_penalty;
    }

    // classify the action family so distance-shaping doesn't fight the new
    // AI states - approaching is the only action that should always be
    // judged by whether it closes distance.
    bool is_approach_action = (action == 0);                    // move_tords_player
    bool is_evasive_action  = (action == 1 || action == 3);     // run_away_from_player, move_behind_cover
    bool is_utility_action  = (action == 2);                    // go_to_pickup_arrow
    bool is_feint_action    = (action == 4 || action == 7);     // bait_player_attack, fake_run_away
    bool is_lateral_action  = (action == 5 || action == 6);     // strafe_around_player, zig_zag_to_player
    bool is_attack_action   = (action == 8 || action == 9);     // melee, ranged

    // 3a. Compute distance FIRST — needed by both anti-spam (3b) and distance shaping (4)
    float current_dist = Vector2Distance(current_ai_pos, player_pos);
    bool is_closing_distance = (current_dist < last_player_distance);

    // 3b. Anti-Spam Check — only meaningful for the direct-approach action.
    // The other states are designed to be held for many consecutive frames
    // (strafe, flee, feints all rely on that), so punishing repetition there
    // would undermine the exact behaviors we want the agent to learn.
    recent_actions.push_back(action);
    if (recent_actions.size() > max_action_history) {
        recent_actions.pop_front();
    }

    if (is_approach_action && recent_actions.size() >= max_action_history) {
        int repeat_count = 0;
        for (int64_t past_action : recent_actions) {
            if (past_action == action) repeat_count++;
        }
        // repeating "approach" without actually closing distance usually
        // means it's stuck on geometry - genuine stuck-detection, not
        // tactical spam
        if (repeat_count == static_cast<int>(max_action_history) && !is_closing_distance) {
            frame_reward -= 0.15f; 
        }
    }

    // 4. Distance Evaluation — only shape reward on distance for the
    // action families where "closer is better" is actually true.
    if (is_approach_action) {
        if (is_closing_distance) {
            frame_reward += 0.12f;
        } else if (current_dist > last_player_distance) {
            frame_reward -= 0.05f;
        } else {
            frame_reward -= 0.02f;
        }

        // proximity bonus only makes sense for the approach action - it's
        // what tells the agent "being close is good" outside of attacks
        float proximity_bonus = std::max(0.0f, (300.0f - current_dist) / 300.0f) * 0.10f;
        frame_reward += proximity_bonus;
    } else if (is_lateral_action) {
        // strafe/zigzag are meant to hover in a good range band rather than
        // close in - reward being in that band instead of raw closing
        constexpr float LATERAL_SWEET_MIN = 100.0f;
        constexpr float LATERAL_SWEET_MAX = 220.0f;
        if (current_dist >= LATERAL_SWEET_MIN && current_dist <= LATERAL_SWEET_MAX) {
            frame_reward += 0.05f;
        }
    }
    // evasive, utility, and feint actions intentionally skip distance
    // shaping - they're not being judged on "did the gap close this frame"

    // 4b. Attack outcome — this is the primary combat signal, wired to the
    // flags your combat code sets when an attack fires/connects.
    if (get_had_hit_this_frame()) {
        frame_reward += 0.5f; // landed a hit on the player - core objective
        set_had_hit_this_frame(false);
    } else if (get_had_attacked_this_frame()) {
        frame_reward -= 0.05f; // swing/shot fired but missed
    }

    if (is_attack_action && !get_had_attacked_this_frame()) {
        // agent chose to attack but it didn't actually fire (on cooldown or
        // out of arrows) - small penalty so it learns to check can_attack()
        // state implicitly rather than spamming the button
        frame_reward -= 0.03f;
    }
    set_attacked_this_frame(false); // consumed either way, don't leak into next frame

    last_player_distance = current_dist;
    last_ai_position = current_ai_pos;

    // clamp the values
    frame_reward = std::clamp(frame_reward, -1.0f, 1.0f);

    step_memory memory_item = {state, action, frame_reward, next_state, is_done};
    replay_memory.push_back(memory_item);

    if (replay_memory.size() > max_memory_capacity) {
        replay_memory.pop_front();
    }

    return frame_reward;
}


void ai_combat_system::train_on_history() {
    if (replay_memory.size() < static_cast<size_t>(batch_size)) return;

    std::random_device rd;
    std::mt19937 g(rd());

    const int target_batches = 12; 
    const int total_samples_needed = target_batches * batch_size;
    const float gamma = 0.99f; // DQN Discount Factor

    std::vector<size_t> prioritized_pool;
    std::vector<size_t> normal_pool;

    // Filter memory index logs out by strict weight priority bounds
    for (size_t idx = 0; idx < replay_memory.size(); ++idx) {
        if (replay_memory[idx].reward < -1.0f) {
            prioritized_pool.push_back(idx);
        } else {
            normal_pool.push_back(idx);
        }
    }

    std::vector<size_t> final_training_order;

    // 1. Force processing of painful mistakes
    if (!prioritized_pool.empty()) {
        std::shuffle(prioritized_pool.begin(), prioritized_pool.end(), g);
        size_t pain_limit = std::min(prioritized_pool.size(), static_cast<size_t>(total_samples_needed * 0.4f));
        final_training_order.insert(final_training_order.end(), prioritized_pool.begin(), prioritized_pool.begin() + pain_limit);
    }

    // 2. Supplement remainders with base uniform experiences
    if (!normal_pool.empty()) {
        std::shuffle(normal_pool.begin(), normal_pool.end(), g);
        size_t normal_needed = total_samples_needed - final_training_order.size();
        size_t normal_limit = std::min(normal_pool.size(), normal_needed);
        final_training_order.insert(final_training_order.end(), normal_pool.begin(), normal_pool.begin() + normal_limit);
    }
    
    std::shuffle(final_training_order.begin(), final_training_order.end(), g);

    // Compute backpropagation step metrics across fixed batch increments
    for (size_t i = 0; i < final_training_order.size(); i += batch_size) {
        size_t end = std::min(i + batch_size, final_training_order.size());
        if ((end - i) < static_cast<size_t>(batch_size)) break; 

        std::vector<torch::Tensor> states, next_states, actions, rewards, masks;

        for (size_t j = i; j < end; ++j) {
            auto& step = replay_memory[final_training_order[j]];
            states.push_back(step.state);
            next_states.push_back(step.next_state);
            actions.push_back(torch::tensor({step.action}, torch::kInt64));
            rewards.push_back(torch::tensor({step.reward}, torch::kFloat32));
            masks.push_back(torch::tensor({step.done ? 1.0f : 0.0f}, torch::kFloat32));
        }

        auto b_states = torch::stack(states).squeeze(1);
        auto b_next_states = torch::stack(next_states).squeeze(1);
        auto b_actions = torch::stack(actions).view({-1, 1});
        auto b_rewards = torch::stack(rewards).view({-1, 1});
        auto b_masks = torch::stack(masks).view({-1, 1});           

        auto current_q = model->forward(b_states).gather(1, b_actions);
        
        torch::Tensor next_q;
        {
            torch::NoGradGuard no_grad;
            if (target_model) {
                // Double DQN: main model picks the action, target model evaluates it
                auto best_actions = model->forward(b_next_states).argmax(1, true);
                next_q = target_model->forward(b_next_states).gather(1, best_actions);
            } else {
                next_q = std::get<0>(model->forward(b_next_states).max(1, true));
            }
        }
        auto target_q = b_rewards + (gamma * next_q * (1.0f - b_masks));

        optimizer->zero_grad(); 
        auto loss = torch::smooth_l1_loss(current_q, target_q.detach());  // Huber loss instead of MSE
        std::cout << "loss: " << loss.item<float>() << std::endl;
    if (std::isnan(loss.item<float>())) {
        std::cout << "!!! NaN LOSS DETECTED !!!" << std::endl;
    }
        loss.backward();

        torch::nn::utils::clip_grad_norm_(model->parameters(), 1.0);
        optimizer->step();      
    }

    // Polyak Soft Target Network Update (Keeps tracking weights aligned smoothly)
    if (target_model) {
        torch::NoGradGuard no_grad;
        auto model_params = model->parameters();
        auto target_params = target_model->parameters();
        for (size_t k = 0; k < model_params.size(); ++k) {
            target_params[k].copy_(0.995f * target_params[k] + 0.005f * model_params[k]);
        }
    }

    // Decrement global randomized explorations step-wise
    if (epsilon > epsilon_min) {
        epsilon -= epsilon_decay;
    }
}


torch::Tensor ai_combat_system::generate_ai_state_tensor(const Player& player, int current_step, bool commit_to_history) {
    Vector2 p_pos = player.get_pos();
    Vector2 last_player_pos = player.get_last_pos();
    Vector2 player_velocity = Vector2Subtract(p_pos, last_player_pos);

    std::vector<float> raw_frame = {
        (p_pos.x - ai_position.x) / 800.0f,
        (p_pos.y - ai_position.y) / 600.0f,
        ai_position.x / 800.0f,
        ai_position.y / 600.0f,
        static_cast<float>(ai_hp) / static_cast<float>(ai_max_hp),
        static_cast<float>(current_step) / 200.0f,
        recent_actions.empty() ? 0.0f : (static_cast<float>(recent_actions.back()) - 4.5f) / 4.5f,
        static_cast<float>(frames_standing_still) / 60.0f,
        std::clamp(player_velocity.x / 10.0f, -1.0f, 1.0f),
        std::clamp(player_velocity.y / 10.0f, -1.0f, 1.0f),
        arrow_exists_on_map ? (cached_closest_arrow_pos.x - ai_position.x) / 800.0f : 0.0f,
        arrow_exists_on_map ? (cached_closest_arrow_pos.y - ai_position.y) / 600.0f : 0.0f
    };

    
    if (commit_to_history) {
        frame_history.push_back(raw_frame);
        // SCOPE FIX: Added class name resolution
        while (frame_history.size() > static_cast<size_t>(ai_combat_system::stack_size)) {
            frame_history.pop_front();
        }
    }

    
    std::vector<std::vector<float>> stacked;
    
    size_t missing = static_cast<size_t>(ai_combat_system::stack_size) - frame_history.size();
    for (size_t i = 0; i < missing; ++i) {
        stacked.push_back(frame_history.empty() ? raw_frame : frame_history.front()); 
    }
    for (const auto& f : frame_history) {
        stacked.push_back(f);
    }

    std::vector<float> flat;
    
    flat.reserve(ai_combat_system::raw_state_size * ai_combat_system::stack_size);
    for (const auto& f : stacked) {
        flat.insert(flat.end(), f.begin(), f.end());
    }

    return torch::tensor(flat, torch::TensorOptions().dtype(torch::kFloat32))
        
        .view({1, ai_combat_system::raw_state_size * ai_combat_system::stack_size});

}









void ai_combat_system::draw() {
    Vector2 ai_size = { 40.0f, 40.0f };

    DrawRectangleV(
        { ai_position.x - ai_size.x / 2.0f, ai_position.y - ai_size.y / 2.0f }, 
        ai_size, 
        WHITE
    );

    DrawRectangleLines(
        static_cast<int>(ai_position.x - ai_size.x / 2.0f), 
        static_cast<int>(ai_position.y - ai_size.y / 2.0f), 
        static_cast<int>(ai_size.x), 
        static_cast<int>(ai_size.y), 
        BLACK
    );
}


void ai_combat_system::save_model(const std::string& path) {
    torch::save(model, path);
}

void ai_combat_system::load_model(const std::string& path) {
    torch::load(model, path);
    // Keep target_model in sync so Double DQN targets aren't stale after a manual load
    torch::NoGradGuard no_grad;
    auto params = model->parameters();
    auto target_params = target_model->parameters();
    for (size_t i = 0; i < params.size(); ++i) {
        target_params[i].copy_(params[i]);
    }
}