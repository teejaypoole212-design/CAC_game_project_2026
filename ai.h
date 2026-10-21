#pragma once



#include "pch.h"

#include "collision.h"
#include "combat.h"

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <unordered_set>
#include <vector>

class Player;          // full definition is pulled in at the bottom of this file

extern bool debug;     // defined in ai.cpp
extern float g_bot_skill_override;   // tests only: >= 0 fixes the sparring bot's skill (0 = sloppy .. 1 = sharp)


enum ai_category { CAT_APPROACH, CAT_RETREAT, CAT_LATERAL, CAT_MELEE,
                   CAT_RANGED, CAT_RESOURCE, CAT_TRICK, CAT_POSITION, CAT_COUNT }; //cat :)


#define AI_STATE_LIST(X) \
    /* --- closing the gap ------------------------------------------------ */ \
    X(APPROACH,          "approach",         0.70f, 1.20f, CAT_APPROACH) \
    X(APPROACH_ZIGZAG,   "approach zigzag",  0.70f, 1.20f, CAT_APPROACH) \
    X(APPROACH_FLANK,    "flank",            0.80f, 1.30f, CAT_APPROACH) \
    X(INTERCEPT,         "intercept",        0.60f, 1.00f, CAT_APPROACH) \
    X(SPRINT_IN,         "sprint in",        0.50f, 0.80f, CAT_APPROACH) \
    /* --- making space --------------------------------------------------- */ \
    X(RETREAT,           "retreat",          0.60f, 1.10f, CAT_RETREAT)  \
    X(RETREAT_ARC,       "retreat arc",      0.70f, 1.20f, CAT_RETREAT)  \
    X(BACKSTEP,          "backstep",         0.22f, 0.28f, CAT_RETREAT)  \
    X(SPRINT_AWAY,       "sprint away",      0.90f, 1.40f, CAT_RETREAT)  \
    X(KITE_TO_RANGE,     "kite to range",    0.80f, 1.40f, CAT_RETREAT)  \
    /* --- lateral / evasive ---------------------------------------------- */ \
    X(STRAFE_CW,         "strafe cw",        0.70f, 1.30f, CAT_LATERAL)  \
    X(STRAFE_CCW,        "strafe ccw",       0.70f, 1.30f, CAT_LATERAL)  \
    X(STRAFE_WEAVE,      "weave",            0.80f, 1.40f, CAT_LATERAL)  \
    X(DODGE_SIDESTEP,    "dodge",            0.24f, 0.30f, CAT_LATERAL)  \
    X(JUKE,              "juke",             0.50f, 0.60f, CAT_LATERAL)  \
    /* --- melee ---------------------------------------------------------- */ \
    X(MELEE_STRIKE,      "melee strike",     0.45f, 0.80f, CAT_MELEE)    \
    X(MELEE_COMBO,       "melee combo",      1.30f, 1.70f, CAT_MELEE)    \
    X(HIT_AND_RUN,       "hit and run",      0.90f, 1.20f, CAT_MELEE)    \
    X(PUNISH_WHIFF,      "punish whiff",     0.50f, 0.90f, CAT_MELEE)    \
    X(COUNTER_STANCE,    "counter stance",   0.70f, 1.10f, CAT_MELEE)    \
    /* --- ranged --------------------------------------------------------- */ \
    X(SHOOT,             "shoot",            0.45f, 0.60f, CAT_RANGED)   \
    X(SHOOT_LEAD,        "shoot (lead)",     0.50f, 0.65f, CAT_RANGED)   \
    X(SHOOT_STRAFE,      "shoot + strafe",   0.80f, 1.10f, CAT_RANGED)   \
    X(SHOOT_RETREAT,     "shoot + retreat",  0.80f, 1.10f, CAT_RANGED)   \
    /* --- resources ------------------------------------------------------ */ \
    X(GRAB_ARROW,        "grab arrow",       0.80f, 3.50f, CAT_RESOURCE) \
    X(GRAB_ARROW_SAFE,   "grab safe arrow",  0.80f, 3.50f, CAT_RESOURCE) \
    /* --- mind games ----------------------------------------------------- */ \
    X(BAIT_PLAYER,       "bait",             0.90f, 1.40f, CAT_TRICK)    \
    X(FAKE_RETREAT,      "fake retreat",     0.90f, 1.30f, CAT_TRICK)    \
    X(FAKE_CHARGE,       "fake charge",      0.70f, 0.90f, CAT_TRICK)    \
    X(HOLD_GROUND,       "hold ground",      0.30f, 0.70f, CAT_TRICK)    \
    /* --- map positioning ------------------------------------------------ */ \
    X(REPOSITION_OPEN,   "reposition",       1.00f, 1.80f, CAT_POSITION) \
    X(MOVE_BEHIND_COVER, "behind cover",     1.00f, 3.00f, CAT_POSITION)

enum ai_state_id : int {
#define X(id, label, dmin, dmax, cat) STATE_##id,
    AI_STATE_LIST(X)
#undef X
    STATE_COUNT
};
static_assert(STATE_COUNT <= 32, "the valid-action mask is a uint32_t");

constexpr int AI_STATE_SIZE = 49;   // observation length, checked at runtime in build_observation()

// ---------------------------------------------------------------------------
using attack_list    = std::vector<std::unique_ptr<base_attack>>;
using attack_spawner = std::function<void(std::unique_ptr<base_attack>)>;

struct ai_hazard { Vector2 pos; float radius; };           // a trap: circle to stay out of

enum class episode_outcome { AI_WON, AI_LOST, TIMEOUT };

// Dueling Q-network: shared trunk, separate value / advantage heads.
struct combat_net_impl_type : torch::nn::Module {
    combat_net_impl_type(int64_t inputs, int64_t actions);
    torch::Tensor forward(torch::Tensor x);

    torch::nn::Linear fc1{nullptr}, fc2{nullptr}, value{nullptr}, advantage{nullptr};
};

// One finished "option" (state) as stored in replay memory.
struct step_memory {
    std::array<float, AI_STATE_SIZE> state;
    std::array<float, AI_STATE_SIZE> next_state;
    uint32_t next_mask;    // which states were legal in next_state
    float    ret;          // discounted reward collected while the state ran
    float    disc;         // gamma^duration  (0 when the episode really ended)
    int      action;
};

// ===========================================================================
class ai_combat_system {
public:
    static constexpr int STATE_SIZE     = AI_STATE_SIZE;
    static constexpr int ACTION_COUNT   = STATE_COUNT;
    static constexpr int raw_state_size = STATE_SIZE;   // legacy names, so the old
    static constexpr int stack_size     = 1;            // main.cpp line still compiles




    Vector2 knockback_vel{0.0f, 0.0f};
    float knockback_time = 0.0f;

    void apply_knockback(Vector2 from_pos, float strength = 220.0f);


    float flash_timer = 0.0f;

    void flash_red(float seconds = 0.5f); //just for a little bit of feedback
    void tick_flash(float dt);
    bool is_flashing() const;

    // sizes are accepted for compatibility but always forced to STATE_SIZE / ACTION_COUNT
    ai_combat_system(int64_t input_size = STATE_SIZE, int64_t action_size = ACTION_COUNT);

    // ---- one call per game tick (Game::update does this) ------------------
    void update(float dt, const Player& player, const attack_list& player_attacks,
                const attack_spawner& spawn_ai_attack);

    // ---- episode control --------------------------------------------------
    void reset_episode(Vector2 ai_start_pos);        // also restores hp, arrows, timers
    void end_episode(episode_outcome outcome);       // safe to call twice
    bool episode_timed_out() const;
    int  get_episode_ticks() const { return episode_ticks; }

    // ---- CONTACT / EVENT HOOKS - call these from your collision code ------
    void take_damage(int amount, Vector2 knockback_pos);                    // ANY damage source: the AI learns from it automatically
    void on_hit_player(int damage, bool was_melee);  // the AI's attack landed
    void on_wall_contact(Vector2 surface_normal = {0.0f, 0.0f});  // walked into a wall (normal points away from the wall)
    void on_trap_contact(float penalty = 0.30f);     // stepped on a trap (apply its damage with take_damage())
    void on_arrow_pickup();                          // arrow_handler already calls this
    // legacy shims (old call sites keep compiling)
    void on_ai_damage() {}
    void on_ai_death() { end_episode(episode_outcome::AI_LOST); }
    void on_ai_win()   { end_episode(episode_outcome::AI_WON); }

    // ---- world information ------------------------------------------------
    void report_arrow(Vector2 pos);                  // arrow::update calls this for every live arrow
    void set_arena(Rectangle bounds) { arena = bounds; }
    void set_obstacles(const std::vector<Rectangle>& walls) { obstacles = walls; }   // solid + usable as cover
    void set_hazards(const std::vector<ai_hazard>& traps)   { hazards = traps; }     // avoided by steering
    const std::vector<Vector2>& get_known_arrows() const { return arrow_positions; }

    // ---- body -------------------------------------------------------------
    Vector2 get_position() const { return ai_position; }
    Vector2 get_pos() const { return ai_position; }
    Vector2 get_velocity() const { return ai_vel; }        // px/s, for debug overlays
    Vector2 get_state_target() const { return aux_pos; }   // what the running state is heading for (grab / cover states)
    const collision_mask& get_hitbox() const { return hitbox; }
    void change_position_x(float dx);
    void change_position_y(float dy);
    int  get_hp() const { return ai_hp; }
    int  get_max_hp() const { return ai_max_hp; }
    void reset_hp() { ai_hp = ai_max_hp; }
    int  get_arrow_count() const { return arrow_count; }
    void add_arrow(int n = 1) { arrow_count += n; }
    bool can_attack() const;

    // ---- learning ---------------------------------------------------------
    int64_t choose_action(torch::Tensor state);      // epsilon-greedy over the *legal* states
    void    train_on_history();                      // a few extra gradient steps (episode end)
    float   get_epsilon() const { return epsilon; }
    void    set_epsilon(float e) { epsilon = e; }
    // DIFFICULTY (no retraining needed). The real dial is the mistake rate: the fraction of decisions where the
    // AI picks a random legal state instead of its best one. Measured vs a mid-skill bot: 0 -> 99.9% AI wins,
    // 0.25 -> 97%, 0.5 -> 78%, 0.75 -> 47%, 0.9 -> 27%.  (works while learning too; it is a floor on exploration)
    void  set_mistake_rate(float r) { mistake_rate = r < 0.0f ? 0.0f : (r > 1.0f ? 1.0f : r); }
    float get_mistake_rate() const { return mistake_rate; }
    // fine-tuning only: these affect arrow duels / dodging, NOT overall strength (tested: 0.7 s reaction + 4x aim noise
    // still wins 99%, because the AI mostly wins through melee spacing)
    void  set_reaction_time(float seconds) { reaction_time = seconds < 0.0f ? 0.0f : seconds; }
    float get_reaction_time() const { return reaction_time; }
    void  set_aim_noise_scale(float s) { aim_noise_scale = s < 0.0f ? 0.0f : s; }
    float get_aim_noise_scale() const { return aim_noise_scale; }
    void    set_learning_enabled(bool on) { learning_enabled = on; }
    bool    is_learning_enabled() const { return learning_enabled; }
    float   get_average_loss() const { return avg_loss; }
    int64_t get_decision_count() const { return decisions_total; }
    size_t  get_replay_size() const { return replay_normal.size() + replay_event.size(); }
    void    save_model(const std::string& path);
    void    load_model(const std::string& path);

    // ---- stats / debug ----------------------------------------------------
    double get_episode_return() const { return episode_return; }
    int    get_episode_damage_dealt() const { return episode_damage_dealt; }
    int    get_episode_damage_taken() const { return episode_damage_taken; }
    int    get_episode_decisions() const { return episode_decisions; }
    int    get_episode_wall_contacts() const { return episode_wall_contacts; }
    int    get_episode_trap_contacts() const { return episode_trap_contacts; }
    uint32_t get_valid_mask() const { return current_valid_mask; }   // states that were legal at the last decision
    // telemetry for the whole session: how often each state was picked and for how many seconds it ran
    int   get_state_picks(int id) const { return state_picks[id]; }
    float get_state_seconds(int id) const { return state_seconds[id]; }
    int    get_state_id() const { return current_state; }
    const char* get_state_name() const;
    void force_next_state(int id) { forced_next_state = id; }   // debug: the next decision picks this state
    static const char* state_name(int id);
    void draw();

    // a state function returns the velocity it wants (px/s) and whether it is done
    struct steer_cmd {
        Vector2 vel{0.0f, 0.0f};
        float   accel = 0.0f;       // 0 = default acceleration
        bool    finished = false;
    };

private:
    // ---- the states (one function each, generated from AI_STATE_LIST) -----
#define X(id, label, dmin, dmax, cat) steer_cmd st_##id();
    AI_STATE_LIST(X)
#undef X
    steer_cmd run_state(int id);

    // ---- perception / decision / motion -----------------------------------
    void perceive(float dt, const Player& player, const attack_list& player_attacks);
    void scan_player_attacks(const attack_list& player_attacks);
    std::array<float, STATE_SIZE> build_observation() const;
    uint32_t compute_valid_mask() const;
    bool decision_due() const;
    void begin_decision();
    void request_interrupt(float delay_seconds);
    void start_state(int id);
    void integrate_motion(float dt, const steer_cmd& cmd);
    Vector2 apply_avoidance(Vector2 desired) const;
    void accumulate_reward(float dt);

    // ---- helpers the states use -------------------------------------------
    bool can_melee_now() const;
    bool can_shoot_now() const;
    bool fire_melee(Vector2 aim_at, float lead_seconds);
    bool fire_ranged(Vector2 aim_at, float noise_degrees);
    Vector2 predicted_player(float seconds) const;
    float edge_distance(Vector2 p) const;
    float room_along(Vector2 from, Vector2 dir) const;
    int   freer_side(Vector2 forward);
    bool  pick_cover_point(Vector2& out, const Vector2* prefer = nullptr) const;
    Vector2 route_around(Vector2 target, float* path_len = nullptr) const;
    Vector2 away_dir() const;
    Vector2 tangent(int sign) const;
    steer_cmd strafe_motion(int sign, float speed_scale);
    steer_cmd grab_motion(float speed);
    float rand01();
    float rand_range(float a, float b);
    int   rand_sign();
    float gauss(float sigma);

    // ---- learning ---------------------------------------------------------
    void build_networks();
    void hard_sync_target();
    void soft_update_target();
    void push_transition(const std::array<float, STATE_SIZE>& next_state, uint32_t next_mask, bool terminal);
    void train_step();

    // ---- body ----
    Vector2 ai_position{60.0f, 540.0f};
    Vector2 last_ai_position{60.0f, 540.0f};
    Vector2 ai_vel{0.0f, 0.0f};
    collision_mask hitbox;
    int   ai_hp = 100, ai_max_hp = 100;
    int   arrow_count = 0;
    float melee_cd = 0.0f, ranged_cd = 0.0f, attack_lock = 0.0f;

    // ---- world ----
    Rectangle arena{0.0f, 0.0f, 1920.0f, 1080.0f};
    std::vector<Rectangle> obstacles;
    std::vector<ai_hazard> hazards;
    std::vector<Vector2> arrow_positions;     // visible this tick
    std::vector<Vector2> pending_arrows;      // being collected for next tick

    // ---- perception ----
    bool    have_prev_player = false;
    Vector2 p_pos{0.0f, 0.0f}, prev_p_pos{0.0f, 0.0f}, p_vel{0.0f, 0.0f};
    float   p_speed = 0.0f;
    float   dist = 1000.0f, prev_dist = 1000.0f, closing_speed = 0.0f;
    Vector2 to_p{1.0f, 0.0f};
    float   player_hp_ratio = 1.0f;
    int     player_arrows = 0;
    float   since_player_melee = 99.0f, since_player_ranged = 99.0f;
    float   since_damage_taken = 99.0f, since_ai_hit = 99.0f;
    bool    player_melee_active = false;
    float   arrow_threat = 0.0f, arrow_tti = 9.0f;
    Vector2 dodge_dir{0.0f, 0.0f};
    bool    threat_prev = false;
    bool    arrow_exists = false;
    Vector2 nearest_arrow{0.0f, 0.0f};
    float   nearest_arrow_dist = 9999.0f, arrow_race = 0.5f;
    std::unordered_set<const base_attack*> seen_player_attacks;

    // ---- state machine ----
    int   current_state = -1;
    float state_time = 0.0f, state_duration = 0.0f;
    bool  state_finished = false, interrupt_requested = false;
    float interrupt_delay = 0.0f;                  // seconds until a requested interrupt may take effect (perception delay)
    float reaction_time = 0.20f, aim_noise_scale = 1.0f, mistake_rate = 0.0f;
    int   state_sign = 1, state_swings = 0;
    float state_swing_time = 0.0f;
    float aux_f[3] = {0.0f, 0.0f, 0.0f};
    Vector2 aux_pos{0.0f, 0.0f};
    bool  aux_locked = false;
    uint32_t current_valid_mask = 0xFFFFFFFFu;
    int forced_next_state = -1;
    std::array<int, STATE_COUNT>   state_picks{};
    std::array<float, STATE_COUNT> state_seconds{};
    const attack_spawner* spawner = nullptr;

    // ---- reward / episode ----
    float pending_event_reward = 0.0f;
    float phi_prev = 0.0f;
    bool  phi_valid = false;
    bool  wall_contact_latched = false, wall_contact_this_tick = false;
    float episode_time = 0.0f;
    int   episode_ticks = 0;
    bool  episode_closed = false;
    double episode_return = 0.0;
    int   episode_damage_dealt = 0, episode_damage_taken = 0, episode_decisions = 0;
    int   episode_wall_contacts = 0, episode_trap_contacts = 0;

    struct option_record {
        std::array<float, STATE_SIZE> state{};
        int   action = 0;
        float ret = 0.0f;
        float disc = 1.0f;
    } pending;
    bool has_pending = false;

    // ---- learning ----
    std::shared_ptr<combat_net_impl_type> model, target_model;
    std::unique_ptr<torch::optim::Adam> optimizer;
    std::vector<step_memory> replay_normal, replay_event;
    size_t normal_head = 0, event_head = 0;
    float   epsilon = 1.0f;
    int64_t decisions_total = 0, train_steps = 0;
    int     decisions_since_train = 0;
    float   avg_loss = 0.0f;
    bool    learning_enabled = true;
    std::mt19937 rng{std::random_device{}()};
    std::vector<float>   sb_state, sb_next, sb_ret, sb_disc, sb_mask;
    std::vector<int64_t> sb_action;
};

// ===========================================================================
//  Scripted sparring partner: pilots the *Player* during fast training so the
//  AI has something that fights back (rusher / kiter / turtle / strafer / dummy,
//  re-rolled every episode). Game::update drives it; see game.cpp.
// ===========================================================================
class sparring_bot {
public:
    void begin_episode(const Player& player);
    float get_skill() const { return skill; }
    const char* name() const {
        static const char* n[] = {"dummy", "rusher", "kiter", "turtle", "strafer"};
        return n[(int)arch];
    }
    void update(float dt, Player& player, const ai_combat_system& ai, const attack_list& ai_attacks,
                const attack_spawner& spawn_player_attack);

private:
    enum archetype { DUMMY, RUSHER, KITER, TURTLE, STRAFER };
    archetype arch = RUSHER;
    float speed = 190.0f, reaction = 0.25f, in_range_time = 0.0f;
    float melee_cd = 0.0f, ranged_cd = 0.0f, lock = 0.0f, dodge_time = 0.0f;
    float skill = 0.5f, dodge_chance = 0.5f, melee_dodge = 0.2f, swing_cd = 1.3f, aim_error = 0.07f;   // all derived from `skill`
    bool  anticipate = false;                       // sharp bots swing early at an opponent that is diving in
    const base_attack* last_dodge_roll = nullptr;   // one dodge roll per incoming swing
    int   ammo = 0, last_seen_amount = 0, strafe_sign = 1;
    float strafe_flip = 0.0f, lunge_timer = 2.0f;
    Vector2 dodge_vec{0.0f, 0.0f};
    std::mt19937 rng{std::random_device{}()};
    float rand01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
};

// Player's header includes this one, so it is pulled in LAST: whichever of the
// two headers a .cpp includes first, both end up fully defined.
#include "player.h"