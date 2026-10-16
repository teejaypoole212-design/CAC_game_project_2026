#include "ai.h"
#include "player.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>

bool debug = true;   // plain global on purpose: other files reference it with `extern`

// ============================================================================
//  1. TUNING  -  every number here
// ============================================================================
namespace {
namespace tune {
    constexpr float PI_F = 3.14159265f;

    // body
    constexpr float BODY_HALF  = 20.0f;    // AI is drawn as a 40x40 square
    constexpr float BODY_CLEAR = 46.0f;    // never walk closer than this to the player's centre
    constexpr float WALL_BLOCK  = BODY_HALF + 4.0f;    // a wall "blocks" a straight path if it comes this close
    constexpr float WALL_CORNER = BODY_HALF + 30.0f;   // detours round a wall's corners with this clearance
    constexpr float COVER_RING  = BODY_HALF + 22.0f;   // stand this far off a wall when hiding behind it

    // locomotion (px/s and px/s^2)
    constexpr float SPEED_WALK   = 180.0f;
    constexpr float SPEED_SPRINT = 260.0f;
    constexpr float SPEED_DASH   = 400.0f;
    constexpr float ACCEL        = 1800.0f;   // ~0.1 s to reach walking speed: no instant 180-degree turns
    constexpr float ACCEL_DASH   = 6500.0f;   // dashes/dodges bypass the slow ramp

    // combat geometry (from combat.cpp: the swing triangle spans ~50-85 px in front of the owner)
    constexpr float  MELEE_TRIGGER = 92.0f;   // start a swing at/below this centre distance
    constexpr float  MELEE_IDEAL   = 72.0f;   // footwork target for melee spacing
    constexpr float  MELEE_SIZE    = 60.0f;
    constexpr double MELEE_DAMAGE  = 15.0;
    constexpr double ARROW_DAMAGE  = 10.0;
    constexpr double ARROW_SPEED   = 450.0;
    constexpr float  BOW_MIN       = 90.0f;   // closer than this melee is the better tool
    constexpr float  BOW_MAX       = 650.0f;  // arrows live 2 s * 450 px/s = 900 px, hit-rate drops fast
    constexpr float  MELEE_COOLDOWN  = 1.0f;  // match these to your player's real cooldowns
    constexpr float  RANGED_COOLDOWN = 1.4f;
    constexpr float  ATTACK_LOCK     = 0.30f; // no second attack of any kind right after one
    constexpr int    STARTING_ARROWS = 0;
    constexpr float  ARROW_HIT_RADIUS = 44.0f;

    // decision making
    constexpr float MIN_COMMIT_SECONDS  = 0.10f;   // a state must run at least this long before it can be interrupted (anti-jitter)
    constexpr float REACTION_SECONDS    = 0.20f;   // perception delay before the AI can react to a hit / incoming arrow
    constexpr float MAX_EPISODE_SECONDS = 45.0f;   // then the round is a timeout (bootstrapped, not terminal)
    constexpr float GAMMA_PER_SEC       = 0.914f;  // ~0.9985 per 60 Hz tick, horizon ~11 s

    // reward
    constexpr float R_DMG_DEALT_PER_HP = 0.05f;    // 15 dmg melee = +0.75, 10 dmg arrow = +0.5
    constexpr float R_DMG_TAKEN_PER_HP = 0.05f;
    constexpr float R_WIN_BASE   = 3.0f;
    constexpr float R_WIN_HP     = 2.0f;           // + this * remaining hp fraction
    constexpr float R_LOSS       = -3.0f;
    constexpr float R_TIME_PER_SEC = 0.05f;
    constexpr float R_ARROW_FIRED  = -0.03f;
    constexpr float R_MELEE_SWING  = -0.01f;
    constexpr float R_WALL_CONTACT = -0.05f;       // once per new contact
    constexpr float R_WALL_PER_SEC = 0.25f;        // while pressed against it
    constexpr float R_OVERLAP_PER_SEC = 0.6f;      // standing inside the player
    constexpr float R_PICKUP_LOW   = 0.15f;
    constexpr float R_PICKUP_HIGH  = 0.03f;
    constexpr float APPROACH_POTENTIAL = 0.30f;    // potential-based (can't be farmed): total bonus for crossing the map

    // learning
    constexpr int   HIDDEN = 192;
    constexpr float LEARNING_RATE = 5e-4f;
    constexpr int   BATCH_SIZE = 64;
    constexpr int   WARMUP_TRANSITIONS = 1500;
    constexpr int   TRAIN_EVERY_DECISIONS = 4;
    constexpr int   EPISODE_END_TRAIN_STEPS = 8;
    constexpr float TAU = 0.005f;
    constexpr float GRAD_CLIP = 10.0f;
    constexpr size_t NORMAL_CAPACITY = 60000;
    constexpr size_t EVENT_CAPACITY  = 15000;      // hits / damage / terminals are kept separately...
    constexpr float EVENT_FRACTION   = 0.30f;      // ...and over-sampled this much
    constexpr float EVENTFUL_RETURN  = 0.30f;
    constexpr float EPS_MIN = 0.05f;
    constexpr float EPS_DECAY_DECISIONS = 150000.0f;
}

// ============================================================================
//  2. HELPERS
// ============================================================================
inline Vector2 V2(float x, float y)        { return Vector2{x, y}; }
inline Vector2 v_add(Vector2 a, Vector2 b) { return Vector2{a.x + b.x, a.y + b.y}; }
inline Vector2 v_sub(Vector2 a, Vector2 b) { return Vector2{a.x - b.x, a.y - b.y}; }
inline Vector2 v_scl(Vector2 a, float s)   { return Vector2{a.x * s, a.y * s}; }
inline float   v_dot(Vector2 a, Vector2 b) { return a.x * b.x + a.y * b.y; }
inline float   v_crs(Vector2 a, Vector2 b) { return a.x * b.y - a.y * b.x; }
inline float   v_len(Vector2 a)            { return std::sqrt(a.x * a.x + a.y * a.y); }
inline float   v_dist(Vector2 a, Vector2 b){ return v_len(v_sub(a, b)); }
inline Vector2 v_perp(Vector2 a)           { return Vector2{-a.y, a.x}; }
inline Vector2 v_norm(Vector2 a, Vector2 fallback = Vector2{0.0f, 0.0f}) {
    float l = v_len(a);
    return l > 1e-5f ? v_scl(a, 1.0f / l) : fallback;
}
inline Vector2 v_rot(Vector2 a, float rad) {
    float c = std::cos(rad), s = std::sin(rad);
    return Vector2{a.x * c - a.y * s, a.x * s + a.y * c};
}
inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
inline float lerpf(float a, float b, float t)    { return a + (b - a) * t; }

// Velocity for a natural-looking backwards walk: speed surges/eases 75-100% on a slow
// sine, plus a slow side-to-side weight shift. (ported from the old ai.cpp)
Vector2 natural_retreat_step(Vector2 retreat_dir, float base_speed, float phase_time) {
    Vector2 perp = v_perp(retreat_dir);
    float speed_wave = 0.75f + 0.25f * std::sin(phase_time * 1.4f);
    float sway = std::sin(phase_time * 0.9f + 1.7f) * 55.0f;
    return v_add(v_scl(retreat_dir, base_speed * speed_wave), v_scl(perp, sway));
}

// does the segment a->b touch rectangle r? (slab test)
bool segment_hits_rect(Vector2 a, Vector2 b, const Rectangle& r) {
    float t0 = 0.0f, t1 = 1.0f;
    const float d[2] = {b.x - a.x, b.y - a.y}, o[2] = {a.x, a.y};
    const float lo[2] = {r.x, r.y}, hi[2] = {r.x + r.width, r.y + r.height};
    for (int i = 0; i < 2; ++i) {
        if (std::fabs(d[i]) < 1e-6f) { if (o[i] < lo[i] || o[i] > hi[i]) return false; continue; }
        float ta = (lo[i] - o[i]) / d[i], tb = (hi[i] - o[i]) / d[i];
        if (ta > tb) std::swap(ta, tb);
        t0 = std::max(t0, ta); t1 = std::min(t1, tb);
        if (t0 > t1) return false;
    }
    return true;
}

// point at fraction t (0..1) along the perimeter of r
Vector2 perimeter_point(const Rectangle& r, float t) {
    float s = t * 2.0f * (r.width + r.height);
    if (s < r.width)  return Vector2{r.x + s, r.y};
    s -= r.width;
    if (s < r.height) return Vector2{r.x + r.width, r.y + s};
    s -= r.height;
    if (s < r.width)  return Vector2{r.x + r.width - s, r.y + r.height};
    s -= r.width;
    return Vector2{r.x, r.y + r.height - s};
}

struct state_info { const char* name; float dur_min, dur_max; int category; };
const state_info STATE_INFO[STATE_COUNT] = {
#define X(id, label, dmin, dmax, cat) { label, dmin, dmax, (int)(cat) },
    AI_STATE_LIST(X)
#undef X
};
} // namespace

const char* ai_combat_system::state_name(int id) {
    return (id >= 0 && id < STATE_COUNT) ? STATE_INFO[id].name : "-";
}
const char* ai_combat_system::get_state_name() const { return state_name(current_state); }

// ============================================================================
//  3. NETWORK + CONSTRUCTOR
// ============================================================================
combat_net_impl_type::combat_net_impl_type(int64_t inputs, int64_t actions) {
    fc1       = register_module("fc1",       torch::nn::Linear(inputs, tune::HIDDEN));
    fc2       = register_module("fc2",       torch::nn::Linear(tune::HIDDEN, tune::HIDDEN));
    value     = register_module("value",     torch::nn::Linear(tune::HIDDEN, 1));
    advantage = register_module("advantage", torch::nn::Linear(tune::HIDDEN, actions));
}

torch::Tensor combat_net_impl_type::forward(torch::Tensor x) {
    x = torch::relu(fc1->forward(x));
    x = torch::relu(fc2->forward(x));
    torch::Tensor v = value->forward(x);
    torch::Tensor a = advantage->forward(x);
    return v + a - a.mean({-1}, true);       // dueling: Q = V + (A - mean A)
}

void ai_combat_system::build_networks() {
    model        = std::make_shared<combat_net_impl_type>(STATE_SIZE, ACTION_COUNT);
    target_model = std::make_shared<combat_net_impl_type>(STATE_SIZE, ACTION_COUNT);
    for (auto& p : target_model->parameters()) p.set_requires_grad(false);
    hard_sync_target();
    optimizer = std::make_unique<torch::optim::Adam>(model->parameters(),
                                                     torch::optim::AdamOptions(tune::LEARNING_RATE));
}

void ai_combat_system::hard_sync_target() {
    torch::NoGradGuard no_grad;
    auto src = model->parameters();
    auto dst = target_model->parameters();
    for (size_t i = 0; i < src.size(); ++i) dst[i].copy_(src[i]);
}

void ai_combat_system::soft_update_target() {
    torch::NoGradGuard no_grad;
    auto src = model->parameters();
    auto dst = target_model->parameters();
    for (size_t i = 0; i < src.size(); ++i) dst[i].mul_(1.0f - tune::TAU).add_(src[i], tune::TAU);
}

ai_combat_system::ai_combat_system(int64_t input_size, int64_t action_size) {
    if (input_size != STATE_SIZE || action_size != ACTION_COUNT) {
        std::printf("[ai] note: constructor asked for %lld inputs / %lld actions; using %d / %d\n",
                    (long long)input_size, (long long)action_size, STATE_SIZE, ACTION_COUNT);
    }
    reaction_time = tune::REACTION_SECONDS;
    torch::set_num_threads(1);     // tiny MLP: one thread is faster than thread hand-off overhead
    build_networks();
    hitbox = collision_mask::make_rect(ai_position, tune::BODY_HALF, tune::BODY_HALF);
    replay_normal.reserve(8192);
    replay_event.reserve(2048);
    sb_state.resize((size_t)tune::BATCH_SIZE * STATE_SIZE);
    sb_next.resize((size_t)tune::BATCH_SIZE * STATE_SIZE);
    sb_mask.resize((size_t)tune::BATCH_SIZE * ACTION_COUNT);
    sb_ret.resize(tune::BATCH_SIZE);
    sb_disc.resize(tune::BATCH_SIZE);
    sb_action.resize(tune::BATCH_SIZE);
    reset_episode(ai_position);
}

float ai_combat_system::rand01() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(rng); }
float ai_combat_system::rand_range(float a, float b) { return a + (b - a) * rand01(); }
int   ai_combat_system::rand_sign() { return rand01() < 0.5f ? -1 : 1; }
float ai_combat_system::gauss(float sigma) { return std::normal_distribution<float>(0.0f, sigma)(rng); }

// ============================================================================
//  4. BODY, HOOKS, EPISODE CONTROL
// ============================================================================
void ai_combat_system::change_position_x(float dx) { ai_position.x += dx; hitbox.set_position(ai_position); }
void ai_combat_system::change_position_y(float dy) { ai_position.y += dy; hitbox.set_position(ai_position); }

bool ai_combat_system::can_melee_now() const { return melee_cd <= 0.0f && attack_lock <= 0.0f; }
bool ai_combat_system::can_shoot_now() const { return arrow_count > 0 && ranged_cd <= 0.0f && attack_lock <= 0.0f; }
bool ai_combat_system::can_attack() const    { return can_melee_now() || can_shoot_now(); }

// Every damage source goes through here, so the AI learns from walls, traps, arrows...
void ai_combat_system::take_damage(int amount) {
    if (amount <= 0) {                                   // negative = heal
        ai_hp = std::min(ai_max_hp, ai_hp - amount);
        return;
    }
    ai_hp -= amount;
    episode_damage_taken += amount;
    pending_event_reward -= tune::R_DMG_TAKEN_PER_HP * (float)amount;
    since_damage_taken = 0.0f;
    request_interrupt(reaction_time * rand_range(0.8f, 1.25f));   // a person re-thinks when hit - after a reaction delay
}

void ai_combat_system::on_hit_player(int damage, bool was_melee) {
    (void)was_melee;
    if (damage <= 0) return;
    episode_damage_dealt += damage;
    pending_event_reward += tune::R_DMG_DEALT_PER_HP * (float)damage;
    since_ai_hit = 0.0f;
}

void ai_combat_system::on_wall_contact(Vector2 surface_normal) {
    (void)surface_normal;
    wall_contact_this_tick = true;
    if (!wall_contact_latched) {                         // penalise the *new* contact once
        wall_contact_latched = true;
        ++episode_wall_contacts;
        pending_event_reward += tune::R_WALL_CONTACT;
        request_interrupt(0.05f);                        // own-body feedback: no perception delay needed
    }
}

void ai_combat_system::on_trap_contact(float penalty) {
    ++episode_trap_contacts;
    pending_event_reward -= std::fabs(penalty);
    request_interrupt(0.05f);
}

void ai_combat_system::on_arrow_pickup() {
    pending_event_reward += (arrow_count <= 3) ? tune::R_PICKUP_LOW : tune::R_PICKUP_HIGH;
}

void ai_combat_system::report_arrow(Vector2 pos) { pending_arrows.push_back(pos); }

bool ai_combat_system::episode_timed_out() const { return episode_time >= tune::MAX_EPISODE_SECONDS; }

void ai_combat_system::reset_episode(Vector2 ai_start_pos) {
    ai_position = ai_start_pos;
    last_ai_position = ai_start_pos;
    ai_vel = V2(0.0f, 0.0f);
    hitbox.set_position(ai_position);
    reset_hp();
    arrow_count = tune::STARTING_ARROWS;
    melee_cd = ranged_cd = attack_lock = 0.0f;

    have_prev_player = false;
    since_player_melee = since_player_ranged = since_damage_taken = since_ai_hit = 99.0f;
    player_melee_active = false;
    arrow_threat = 0.0f; arrow_tti = 9.0f; threat_prev = false; dodge_dir = V2(0.0f, 0.0f);
    arrow_exists = false; arrow_positions.clear(); pending_arrows.clear();
    seen_player_attacks.clear();

    current_state = -1;
    state_time = 0.0f; state_finished = false; interrupt_requested = false; interrupt_delay = 0.0f;
    has_pending = false;
    pending_event_reward = 0.0f;
    phi_valid = false;
    wall_contact_latched = wall_contact_this_tick = false;

    episode_time = 0.0f; episode_ticks = 0; episode_closed = false;
    episode_return = 0.0; episode_damage_dealt = episode_damage_taken = episode_decisions = 0;
    episode_wall_contacts = episode_trap_contacts = 0;
}

// Close the running state with its final reward and store it. Safe to call twice.
void ai_combat_system::end_episode(episode_outcome outcome) {
    if (episode_closed) return;
    episode_closed = true;

    float r = pending_event_reward;
    pending_event_reward = 0.0f;
    const float hp_frac = (float)std::max(ai_hp, 0) / (float)ai_max_hp;
    if (outcome == episode_outcome::AI_WON)  r += tune::R_WIN_BASE + tune::R_WIN_HP * hp_frac;
    if (outcome == episode_outcome::AI_LOST) r += tune::R_LOSS;
    episode_return += r;

    if (has_pending) {
        pending.ret += pending.disc * r;
        // a timeout is NOT a real ending: keep bootstrapping from the final state
        push_transition(build_observation(), compute_valid_mask(), outcome != episode_outcome::TIMEOUT);
        has_pending = false;
    }
}

// ============================================================================
//  5. PERCEPTION  - what a human would see on screen
// ============================================================================
float ai_combat_system::edge_distance(Vector2 p) const {
    return std::min(std::min(p.x - arena.x, arena.x + arena.width - p.x),
                    std::min(p.y - arena.y, arena.y + arena.height - p.y));
}

// how far the body can travel from `from` along `dir` before touching the arena edge
float ai_combat_system::room_along(Vector2 from, Vector2 dir) const {
    const float minx = arena.x + tune::BODY_HALF, maxx = arena.x + arena.width  - tune::BODY_HALF;
    const float miny = arena.y + tune::BODY_HALF, maxy = arena.y + arena.height - tune::BODY_HALF;
    float t = 1e9f;
    if (dir.x >  1e-4f) t = std::min(t, (maxx - from.x) / dir.x);
    if (dir.x < -1e-4f) t = std::min(t, (minx - from.x) / dir.x);
    if (dir.y >  1e-4f) t = std::min(t, (maxy - from.y) / dir.y);
    if (dir.y < -1e-4f) t = std::min(t, (miny - from.y) / dir.y);
    return std::max(t, 0.0f);
}

// which perpendicular (+1 / -1) of `forward` has more open floor; random when it's a wash
int ai_combat_system::freer_side(Vector2 forward) {
    Vector2 perp = v_perp(forward);
    float a = room_along(ai_position, perp);
    float b = room_along(ai_position, v_scl(perp, -1.0f));
    if (std::fabs(a - b) < 40.0f) return rand_sign();
    return a > b ? 1 : -1;
}

Vector2 ai_combat_system::away_dir() const { return v_scl(to_p, -1.0f); }
Vector2 ai_combat_system::tangent(int sign) const { return v_scl(v_perp(to_p), (float)sign); }

Vector2 ai_combat_system::predicted_player(float seconds) const {
    Vector2 p = v_add(p_pos, v_scl(p_vel, seconds));
    p.x = clampf(p.x, arena.x, arena.x + arena.width);
    p.y = clampf(p.y, arena.y, arena.y + arena.height);
    return p;
}

// Best hiding spot: the cheapest-to-reach point, on a ring around a wall, from which the wall hides the
// player's line of sight. `prefer` (last tick's pick) gets a small bonus so the AI doesn't dither.
bool ai_combat_system::pick_cover_point(Vector2& out, const Vector2* prefer) const {
    if (obstacles.empty()) return false;
    float best = 1e9f;
    bool found = false;
    for (const Rectangle& w : obstacles) {
        const float r = tune::COVER_RING;
        const Rectangle ring{w.x - r, w.y - r, w.width + 2.0f * r, w.height + 2.0f * r};
        const float inset = std::min(10.0f, 0.25f * std::min(w.width, w.height));
        const Rectangle shade{w.x + inset, w.y + inset, w.width - 2.0f * inset, w.height - 2.0f * inset};   // the line must cross the wall's body
        for (int i = 0; i < 36; ++i) {
            Vector2 cand = perimeter_point(ring, (float)i / 36.0f);
            if (cand.x < arena.x + tune::BODY_HALF || cand.x > arena.x + arena.width  - tune::BODY_HALF ||
                cand.y < arena.y + tune::BODY_HALF || cand.y > arena.y + arena.height - tune::BODY_HALF) continue;
            if (!segment_hits_rect(cand, p_pos, shade)) continue;                       // still in view
            bool bad = false;
            for (const auto& hz : hazards) if (v_dist(hz.pos, cand) < hz.radius + tune::BODY_HALF + 10.0f) bad = true;
            if (bad) continue;
            float len = 0.0f;
            route_around(cand, &len);
            if (prefer && v_dist(*prefer, cand) < 35.0f) len -= 40.0f;
            if (len < best) { best = len; out = cand; found = true; }
        }
    }
    return found;
}

// Shortest way round the walls to `target` (single convex obstacles; the first blocking wall wins).
// Returns the next waypoint to head for, and the total path length via `path_len`.
Vector2 ai_combat_system::route_around(Vector2 target, float* path_len) const {
    for (const Rectangle& w : obstacles) {
        const float pb = tune::WALL_BLOCK, pc = tune::WALL_CORNER;
        const Rectangle block{w.x - pb, w.y - pb, w.width + 2.0f * pb, w.height + 2.0f * pb};
        if (!segment_hits_rect(ai_position, target, block)) continue;
        const Vector2 c[4] = {{w.x - pc, w.y - pc}, {w.x + w.width + pc, w.y - pc},
                              {w.x + w.width + pc, w.y + w.height + pc}, {w.x - pc, w.y + w.height + pc}};
        float best = 1e9f;
        Vector2 first = target, second = target;
        for (int i = 0; i < 4; ++i) {
            const float l1 = v_dist(ai_position, c[i]);
            if (l1 > 20.0f && segment_hits_rect(ai_position, c[i], block)) continue;   // can't see this corner from here
            if (!segment_hits_rect(c[i], target, block) && l1 + v_dist(c[i], target) < best) {
                best = l1 + v_dist(c[i], target); first = c[i]; second = target;       // one corner is enough
            }
            for (int s = -1; s <= 1; s += 2) {                                         // or round two neighbouring corners
                const Vector2& cj = c[(i + 4 + s) % 4];
                if (segment_hits_rect(cj, target, block)) continue;
                const float cost = l1 + v_dist(c[i], cj) + v_dist(cj, target);
                if (cost < best) { best = cost; first = c[i]; second = cj; }
            }
        }
        if (best >= 1e8f) { if (path_len) *path_len = 2.0f * v_dist(ai_position, target); return target; }   // no clean route: push on, avoidance slides us
        if (path_len) *path_len = best;
        return (v_dist(ai_position, first) > 20.0f) ? first : second;                  // reached the corner -> next waypoint
    }
    if (path_len) *path_len = v_dist(ai_position, target);
    return target;
}

void ai_combat_system::scan_player_attacks(const attack_list& player_attacks) {
    arrow_threat = 0.0f; arrow_tti = 9.0f; dodge_dir = V2(0.0f, 0.0f);
    player_melee_active = false;
    std::unordered_set<const base_attack*> now;

    for (const auto& up : player_attacks) {
        const base_attack* a = up.get();
        if (!a || !a->is_Alive()) continue;
        now.insert(a);
        const bool is_arrow = dynamic_cast<const ranged_attack*>(a) != nullptr;

        if (!seen_player_attacks.count(a)) {            // first time we see this attack = the player just used it
            if (is_arrow) since_player_ranged = 0.0f; else since_player_melee = 0.0f;
        }
        if (!is_arrow) { player_melee_active = true; continue; }

        // incoming arrow: where is the closest approach along its flight line?
        const Vector2 ad = a->get_dir();
        const float speed = std::max((float)a->get_speed(), 1.0f);
        const Vector2 rel = v_sub(ai_position, a->get_pos());
        const float along = v_dot(rel, ad);
        if (along <= 0.0f) continue;                    // already past us
        const float lateral = v_crs(ad, rel);           // signed distance from the flight line
        const float tti = along / speed;
        if (std::fabs(lateral) >= tune::ARROW_HIT_RADIUS || tti >= 1.3f) continue;

        if (tti < arrow_tti) {
            arrow_tti = tti;
            arrow_threat = clampf(1.0f - std::fabs(lateral) / tune::ARROW_HIT_RADIUS, 0.0f, 1.0f)
                         * clampf(1.4f - tti, 0.0f, 1.0f);
            Vector2 n = v_perp(ad);                     // dodge to the side we're already on...
            float side = (lateral >= 0.0f) ? 1.0f : -1.0f;
            if (std::fabs(lateral) < 4.0f) side = (float)freer_side(ad);
            Vector2 dir = v_scl(n, side);
            if (room_along(ai_position, dir) < 70.0f) dir = v_scl(dir, -1.0f);   // ...unless a wall is there
            dodge_dir = dir;
        }
    }
    seen_player_attacks.swap(now);
}

void ai_combat_system::perceive(float dt, const Player& player, const attack_list& player_attacks) {
    // arrows seen this tick = what arrow::update reported at the end of the previous tick
    arrow_positions.swap(pending_arrows);
    pending_arrows.clear();

    const Vector2 pp = player.get_pos();
    if (!have_prev_player) {                            // first tick of an episode (or after a teleport)
        prev_p_pos = pp; p_vel = V2(0.0f, 0.0f); closing_speed = 0.0f;
        prev_dist = v_dist(pp, ai_position);
        have_prev_player = true;
    } else {
        Vector2 step = v_sub(pp, prev_p_pos);
        Vector2 raw = (v_len(step) > 200.0f) ? V2(0.0f, 0.0f) : v_scl(step, 1.0f / dt);   // ignore teleports
        p_vel = V2(lerpf(p_vel.x, raw.x, 0.35f), lerpf(p_vel.y, raw.y, 0.35f));
    }
    prev_p_pos = pp;
    p_pos = pp;
    p_speed = v_len(p_vel);

    const Vector2 d = v_sub(pp, ai_position);
    dist = v_len(d);
    to_p = v_norm(d, V2(1.0f, 0.0f));
    closing_speed = lerpf(closing_speed, clampf((prev_dist - dist) / dt, -600.0f, 600.0f), 0.3f);
    prev_dist = dist;

    player_hp_ratio = clampf((float)player.get_health() / (float)std::max(player.get_max_health(), 1), 0.0f, 1.0f);
    player_arrows = player.get_ranged_amount();

    since_player_melee += dt; since_player_ranged += dt;
    since_damage_taken += dt; since_ai_hit += dt;
    scan_player_attacks(player_attacks);

    arrow_exists = !arrow_positions.empty();
    if (arrow_exists) {
        nearest_arrow_dist = 1e9f;
        for (const Vector2& a : arrow_positions) {
            float da = v_dist(a, ai_position);
            if (da < nearest_arrow_dist) { nearest_arrow_dist = da; nearest_arrow = a; }
        }
        arrow_race = (v_dist(nearest_arrow, pp) > nearest_arrow_dist) ? 1.0f : 0.0f;   // 1 = I get there first
    } else {
        nearest_arrow_dist = 9999.0f; arrow_race = 0.5f;
    }

    const bool threat_now = arrow_threat > 0.35f;       // an arrow just became dangerous -> re-think
    if (threat_now && !threat_prev) request_interrupt(reaction_time * rand_range(0.8f, 1.25f));
    threat_prev = threat_now;
}

// ============================================================================
//  6. OBSERVATION (what the network sees) + ACTION MASK (what it may pick)
// ============================================================================
std::array<float, ai_combat_system::STATE_SIZE> ai_combat_system::build_observation() const {
    std::array<float, STATE_SIZE> f{};
    int n = 0;
    auto put = [&](float v) { if (n < STATE_SIZE) f[n] = std::isfinite(v) ? v : 0.0f; ++n; };

    const float W = arena.width, H = arena.height;
    const Vector2 rel = v_sub(p_pos, ai_position);
    const float melee_ready  = 1.0f - clampf(melee_cd / tune::MELEE_COOLDOWN, 0.0f, 1.0f);
    const float ranged_ready = arrow_count > 0 ? 1.0f - clampf(ranged_cd / tune::RANGED_COOLDOWN, 0.0f, 1.0f) : 0.0f;
    bool hazard_near = false;
    for (const auto& h : hazards) if (v_dist(h.pos, ai_position) < h.radius + 150.0f) hazard_near = true;

    put(rel.x / W);                                      //  0-1  where the player is, relative to me
    put(rel.y / H);
    put(std::min(dist, 1200.0f) / 1200.0f);              //  2    distance
    put(to_p.x); put(to_p.y);                            //  3-4  unit direction to the player
    put((ai_position.x - arena.x) / W * 2.0f - 1.0f);    //  5-6  my absolute position
    put((ai_position.y - arena.y) / H * 2.0f - 1.0f);
    put((p_pos.x - arena.x) / W * 2.0f - 1.0f);          //  7-8  player absolute position
    put((p_pos.y - arena.y) / H * 2.0f - 1.0f);
    put(std::min(edge_distance(ai_position), 540.0f) / 540.0f);   //  9  am I near a wall / corner
    put(std::min(edge_distance(p_pos), 540.0f) / 540.0f);         // 10  is the player
    put(ai_vel.x / tune::SPEED_DASH); put(ai_vel.y / tune::SPEED_DASH);   // 11-12 my velocity
    put(clampf(p_vel.x / 300.0f, -1.0f, 1.0f));          // 13-14 player velocity (estimated)
    put(clampf(p_vel.y / 300.0f, -1.0f, 1.0f));
    put(clampf(closing_speed / 300.0f, -1.0f, 1.0f));    // 15   >0 = gap is closing
    const float my_hp = (float)std::max(ai_hp, 0) / (float)ai_max_hp;
    put(my_hp); put(player_hp_ratio); put(my_hp - player_hp_ratio);      // 16-18
    put(std::min((float)arrow_count, 5.0f) / 5.0f);      // 19   my arrows
    put(std::min((float)player_arrows, 5.0f) / 5.0f);    // 20   player arrows picked up
    put(melee_ready); put(ranged_ready);                 // 21-22 attack readiness
    put(dist <= tune::MELEE_TRIGGER ? 1.0f : 0.0f);      // 23   in melee range
    put((dist >= tune::BOW_MIN && dist <= tune::BOW_MAX) ? 1.0f : 0.0f);  // 24 in bow band
    put(std::min(since_player_melee, 3.0f) / 3.0f);      // 25-26 how recently the player attacked
    put(std::min(since_player_ranged, 3.0f) / 3.0f);
    put(player_melee_active ? 1.0f : 0.0f);              // 27
    put(arrow_threat);                                   // 28   incoming arrow danger
    put(dodge_dir.x * arrow_threat); put(dodge_dir.y * arrow_threat);     // 29-30 which way to dodge
    put(arrow_threat > 0.0f ? std::min(arrow_tti, 1.3f) / 1.3f : 1.0f);   // 31 time to impact
    put(arrow_exists ? 1.0f : 0.0f);                     // 32   pickups on the map
    put(arrow_exists ? (nearest_arrow.x - ai_position.x) / W : 0.0f);     // 33-34
    put(arrow_exists ? (nearest_arrow.y - ai_position.y) / H : 0.0f);
    put(arrow_exists ? std::min(nearest_arrow_dist, 1200.0f) / 1200.0f : 1.0f);   // 35
    put(arrow_race);                                     // 36   1 = I'm closer to it than the player
    put(obstacles.empty() ? 0.0f : 1.0f);                // 37   cover exists
    put(hazard_near ? 1.0f : 0.0f);                      // 38   a trap is close
    put(std::min(since_damage_taken, 3.0f) / 3.0f);      // 39   recently hurt
    put(std::min(since_ai_hit, 3.0f) / 3.0f);            // 40   recently landed a hit
    const int cat = current_state >= 0 ? STATE_INFO[current_state].category : -1;
    for (int c = 0; c < CAT_COUNT; ++c) put(c == cat ? 1.0f : 0.0f);      // 41-48 what I was just doing

    if (n != STATE_SIZE) {
        static bool warned = false;
        if (!warned) { std::fprintf(stderr, "[ai] BUG: observation has %d values, AI_STATE_SIZE is %d\n", n, STATE_SIZE); warned = true; }
    }
    return f;
}

// bit i set = state i may be chosen right now
uint32_t ai_combat_system::compute_valid_mask() const {
    uint32_t m = 0;
    auto allow = [&](int id, bool ok) { if (ok) m |= (1u << id); };

    const bool melee_soon = melee_cd <= 0.10f && attack_lock <= 0.10f;
    const bool shot_ok = arrow_count > 0 && ranged_cd <= 0.10f && attack_lock <= 0.10f
                         && dist >= tune::BOW_MIN && dist <= tune::BOW_MAX;
    const float center_dist = v_dist(ai_position, V2(arena.x + arena.width * 0.5f, arena.y + arena.height * 0.5f));
    Vector2 cover;
    float cover_len = 1e9f;
    const bool has_cover = pick_cover_point(cover);
    if (has_cover) route_around(cover, &cover_len);

    allow(STATE_APPROACH,          dist > tune::MELEE_IDEAL + 25.0f);
    allow(STATE_APPROACH_ZIGZAG,   dist > 160.0f);
    allow(STATE_APPROACH_FLANK,    dist > 150.0f);
    allow(STATE_INTERCEPT,         p_speed > 35.0f && dist > 130.0f);
    allow(STATE_SPRINT_IN,         dist > 110.0f && dist < 300.0f);   // a dive only covers ~230 px

    allow(STATE_RETREAT,           dist < 400.0f);          // (done at 430 px: never legal when already done)
    allow(STATE_RETREAT_ARC,       dist < 370.0f);          // (done at 400 px)
    allow(STATE_BACKSTEP,          dist < 260.0f);
    allow(STATE_SPRINT_AWAY,       dist < 600.0f);          // (done at 650 px)
    allow(STATE_KITE_TO_RANGE,     dist < 330.0f);

    allow(STATE_STRAFE_CW,         dist < 650.0f);
    allow(STATE_STRAFE_CCW,        dist < 650.0f);
    allow(STATE_STRAFE_WEAVE,      dist < 700.0f);
    allow(STATE_DODGE_SIDESTEP,    arrow_threat > 0.20f);
    allow(STATE_JUKE,              dist < 420.0f);

    allow(STATE_MELEE_STRIKE,      melee_soon && dist <= 125.0f);
    allow(STATE_MELEE_COMBO,       melee_soon && dist <= 240.0f);
    allow(STATE_HIT_AND_RUN,       melee_soon && dist <= 170.0f);
    allow(STATE_PUNISH_WHIFF,      melee_soon && since_player_melee < 1.8f && dist <= 330.0f);
    allow(STATE_COUNTER_STANCE,    melee_soon && dist > 60.0f && dist <= 280.0f);

    allow(STATE_SHOOT,             shot_ok);
    allow(STATE_SHOOT_LEAD,        shot_ok);
    allow(STATE_SHOOT_STRAFE,      shot_ok);
    allow(STATE_SHOOT_RETREAT,     shot_ok && dist < 520.0f);

    allow(STATE_GRAB_ARROW,        arrow_exists && nearest_arrow_dist < 650.0f);
    allow(STATE_GRAB_ARROW_SAFE,   arrow_exists && nearest_arrow_dist < 650.0f);

    allow(STATE_BAIT_PLAYER,       dist > 90.0f && dist < 420.0f);
    allow(STATE_FAKE_RETREAT,      dist < 300.0f && melee_cd <= 0.5f);
    allow(STATE_FAKE_CHARGE,       dist > 260.0f);
    allow(STATE_HOLD_GROUND,       true);

    allow(STATE_REPOSITION_OPEN,   center_dist > 200.0f || edge_distance(ai_position) < 150.0f);
    allow(STATE_MOVE_BEHIND_COVER, has_cover && cover_len < 650.0f);

    if (m == 0) m = 1u << STATE_HOLD_GROUND;             // cannot happen, but never return "nothing"
    return m;
}

// ============================================================================
//  7. DECISION LOOP + MOTION
// ============================================================================
bool ai_combat_system::decision_due() const {
    if (current_state < 0 || state_finished) return true;
    if (state_time >= state_duration) return true;
    return interrupt_requested && interrupt_delay <= 0.0f && state_time >= tune::MIN_COMMIT_SECONDS;   // hurt / arrow incoming / blocked
}

// Something happened that a person would re-think about: allow a new decision after `delay_seconds`.
void ai_combat_system::request_interrupt(float delay_seconds) {
    if (!interrupt_requested) { interrupt_requested = true; interrupt_delay = delay_seconds; }
    else interrupt_delay = std::min(interrupt_delay, delay_seconds);
}

void ai_combat_system::begin_decision() {
    const std::array<float, STATE_SIZE> obs = build_observation();
    const uint32_t mask = compute_valid_mask();
    current_valid_mask = mask;

    if (has_pending) {                                   // the previous state is now a finished transition
        push_transition(obs, mask, false);
        if (learning_enabled && ++decisions_since_train >= tune::TRAIN_EVERY_DECISIONS) {
            decisions_since_train = 0;
            train_step();
        }
    }

    torch::Tensor st = torch::from_blob(const_cast<float*>(obs.data()), {1, STATE_SIZE}, torch::kFloat32).clone();
    int a = (int)choose_action(st);
    if (forced_next_state >= 0 && forced_next_state < STATE_COUNT) { a = forced_next_state; forced_next_state = -1; }
    start_state(a);

    pending.state = obs; pending.action = a; pending.ret = 0.0f; pending.disc = 1.0f;
    has_pending = true;
    ++decisions_total; ++episode_decisions;
    if (learning_enabled && epsilon > tune::EPS_MIN)
        epsilon = std::max(tune::EPS_MIN, epsilon - (1.0f - tune::EPS_MIN) / tune::EPS_DECAY_DECISIONS);
    interrupt_requested = false;
    interrupt_delay = 0.0f;
}

void ai_combat_system::start_state(int id) {
    current_state = id;
    ++state_picks[id];
    state_time = 0.0f;
    state_finished = false;
    state_duration = rand_range(STATE_INFO[id].dur_min, STATE_INFO[id].dur_max);
    state_sign = rand_sign();
    state_swings = 0; state_swing_time = 0.0f;
    aux_f[0] = aux_f[1] = aux_f[2] = 0.0f;
    aux_pos = V2(0.0f, 0.0f);
    aux_locked = false;                                  // states do their own set-up on their first tick
}

ai_combat_system::steer_cmd ai_combat_system::run_state(int id) {
    switch (id) {
#define X(sid, label, dmin, dmax, cat) case STATE_##sid: return st_##sid();
        AI_STATE_LIST(X)
#undef X
        default: break;
    }
    return steer_cmd{};
}

// Soft steering layer applied under EVERY state: fade motion into walls, traps and the
// player's body so no state has to worry about them (hard clamps happen in integrate_motion).
Vector2 ai_combat_system::apply_avoidance(Vector2 desired) const {
    const float margin = 85.0f;
    auto soften = [&](float room, float& comp, float inward_sign) {
        if (room >= margin) return;
        float w = clampf(1.0f - room / margin, 0.0f, 1.0f);
        if (comp * inward_sign < 0.0f) comp *= (1.0f - w);   // heading into the wall: fade it out
        comp += inward_sign * w * 60.0f;                     // and nudge back toward open floor
    };
    soften(ai_position.x - arena.x - tune::BODY_HALF,                 desired.x,  1.0f);
    soften(arena.x + arena.width  - ai_position.x - tune::BODY_HALF,  desired.x, -1.0f);
    soften(ai_position.y - arena.y - tune::BODY_HALF,                 desired.y,  1.0f);
    soften(arena.y + arena.height - ai_position.y - tune::BODY_HALF,  desired.y, -1.0f);

    for (const auto& h : hazards) {                          // traps
        Vector2 d = v_sub(ai_position, h.pos);
        float dd = v_len(d), R = h.radius + tune::BODY_HALF + 55.0f;
        if (dd >= R || dd < 1e-3f) continue;
        Vector2 n = v_scl(d, 1.0f / dd);
        float w = 1.0f - dd / R, into = v_dot(desired, n);
        if (into < 0.0f) desired = v_sub(desired, v_scl(n, into * w));
        desired = v_add(desired, v_scl(n, w * 220.0f));
    }
    for (const Rectangle& wall : obstacles) {                // walls: slide along the surface, never push into it
        const float nx = clampf(ai_position.x, wall.x, wall.x + wall.width);      // nearest point on the wall
        const float ny = clampf(ai_position.y, wall.y, wall.y + wall.height);
        const Vector2 d = V2(ai_position.x - nx, ai_position.y - ny);
        const float dd = v_len(d);
        if (dd >= tune::BODY_HALF + 14.0f) continue;         // (route waypoints sit further out, so they are never blocked)
        const Vector2 centre = V2(wall.x + wall.width * 0.5f, wall.y + wall.height * 0.5f);
        const Vector2 n = (dd > 1e-3f) ? v_scl(d, 1.0f / dd) : v_norm(v_sub(ai_position, centre), V2(1.0f, 0.0f));
        const float into = v_dot(desired, n);
        if (into < 0.0f) desired = v_sub(desired, v_scl(n, into));
    }
    if (dist < tune::BODY_CLEAR + 6.0f) {                    // don't walk through the player
        float into = v_dot(desired, to_p);
        if (into > 0.0f) desired = v_sub(desired, v_scl(to_p, into));
    }
    return desired;
}

void ai_combat_system::integrate_motion(float dt, const steer_cmd& cmd) {
    const Vector2 desired = apply_avoidance(cmd.vel);
    const float max_delta = (cmd.accel > 0.0f ? cmd.accel : tune::ACCEL) * dt;
    Vector2 dv = v_sub(desired, ai_vel);
    float dl = v_len(dv);
    ai_vel = (dl <= max_delta) ? desired : v_add(ai_vel, v_scl(dv, max_delta / dl));

    Vector2 np = v_add(ai_position, v_scl(ai_vel, dt));

    // hard arena clamp -> wall contact
    const float minx = arena.x + tune::BODY_HALF, maxx = arena.x + arena.width  - tune::BODY_HALF;
    const float miny = arena.y + tune::BODY_HALF, maxy = arena.y + arena.height - tune::BODY_HALF;
    Vector2 hit_normal = V2(0.0f, 0.0f);
    if (np.x < minx) { np.x = minx; ai_vel.x = std::max(ai_vel.x, 0.0f); hit_normal.x =  1.0f; }
    if (np.x > maxx) { np.x = maxx; ai_vel.x = std::min(ai_vel.x, 0.0f); hit_normal.x = -1.0f; }
    if (np.y < miny) { np.y = miny; ai_vel.y = std::max(ai_vel.y, 0.0f); hit_normal.y =  1.0f; }
    if (np.y > maxy) { np.y = maxy; ai_vel.y = std::min(ai_vel.y, 0.0f); hit_normal.y = -1.0f; }

    // solid obstacles: push out along the axis of least penetration
    for (const Rectangle& w : obstacles) {
        const float ex = w.x - tune::BODY_HALF, ey = w.y - tune::BODY_HALF;
        const float ew = w.width + 2.0f * tune::BODY_HALF, eh = w.height + 2.0f * tune::BODY_HALF;
        if (np.x <= ex || np.x >= ex + ew || np.y <= ey || np.y >= ey + eh) continue;
        const float pl = np.x - ex, pr = ex + ew - np.x, pt = np.y - ey, pb = ey + eh - np.y;
        const float m = std::min(std::min(pl, pr), std::min(pt, pb));
        if (m == pl)      { np.x = ex;      hit_normal.x = -1.0f; ai_vel.x = std::min(ai_vel.x, 0.0f); }
        else if (m == pr) { np.x = ex + ew; hit_normal.x =  1.0f; ai_vel.x = std::max(ai_vel.x, 0.0f); }
        else if (m == pt) { np.y = ey;      hit_normal.y = -1.0f; ai_vel.y = std::min(ai_vel.y, 0.0f); }
        else              { np.y = ey + eh; hit_normal.y =  1.0f; ai_vel.y = std::max(ai_vel.y, 0.0f); }
    }
    if (hit_normal.x != 0.0f || hit_normal.y != 0.0f) on_wall_contact(hit_normal);

    ai_position = np;
    hitbox.set_position(ai_position);
}

void ai_combat_system::accumulate_reward(float dt) {
    const float gamma_dt = std::pow(tune::GAMMA_PER_SEC, dt);
    float r = pending_event_reward;                      // hits, damage, pickups, wall bumps since last tick
    pending_event_reward = 0.0f;
    r -= tune::R_TIME_PER_SEC * dt;                      // be decisive

    // potential-based shaping (can't be farmed): a little pull toward the fight from across the map
    const float phi = -tune::APPROACH_POTENTIAL * clampf((dist - 250.0f) / 1500.0f, 0.0f, 1.0f);
    if (phi_valid) r += gamma_dt * phi - phi_prev;
    phi_prev = phi; phi_valid = true;

    if (dist < 38.0f)           r -= tune::R_OVERLAP_PER_SEC * dt;   // standing inside the player
    if (wall_contact_this_tick) r -= tune::R_WALL_PER_SEC * dt;      // hugging a wall

    episode_return += r;
    if (has_pending) { pending.ret += pending.disc * r; pending.disc *= gamma_dt; }
}

void ai_combat_system::update(float dt, const Player& player, const attack_list& player_attacks,
                              const attack_spawner& spawn_ai_attack) {
    if (episode_closed) return;                          // round is over; wait for reset_episode()
    dt = clampf(dt, 1e-4f, 0.05f);
    spawner = &spawn_ai_attack;

    melee_cd = std::max(0.0f, melee_cd - dt);
    ranged_cd = std::max(0.0f, ranged_cd - dt);
    attack_lock = std::max(0.0f, attack_lock - dt);

    perceive(dt, player, player_attacks);
    if (interrupt_requested) interrupt_delay -= dt;

    if (decision_due()) begin_decision();
    steer_cmd cmd = run_state(current_state);
    if (cmd.finished) {
        state_finished = true;                           // re-decide next tick...
        if (state_time > 0.0f) {                         // ...or right now, unless the state is brand new:
            begin_decision();                            // EVERY state occupies at least one tick. A zero-time option has
            cmd = run_state(current_state);              // no time cost and no discount, and the network happily
            state_finished = cmd.finished;               // loops it forever instead of fighting (seen in testing).
        }
    }

    integrate_motion(dt, cmd);
    accumulate_reward(dt);

    state_seconds[current_state] += dt;
    state_time += dt; episode_time += dt; ++episode_ticks;
    if (!wall_contact_this_tick) wall_contact_latched = false;       // contact ended
    wall_contact_this_tick = false;
    last_ai_position = ai_position;
    spawner = nullptr;
}

// ============================================================================
//  8. THE 32 STATES
//  Each st_NAME() runs once per tick while it is active and returns the velocity it
//  wants (px/s) + whether it is finished. Available to every state:
//    state_time (s since start)   state_duration (planned length)   state_sign (+-1 random)
//    dist / to_p / p_pos / p_vel  (the player)    aux_f[3], aux_pos, aux_locked (scratch)
//    fire_melee() / fire_ranged() (obey cooldowns; return true when they actually fired)
//  Movement is smoothed and wall/trap/player-body-safe automatically (integrate_motion).
// ============================================================================
bool ai_combat_system::fire_melee(Vector2 aim_at, float lead_seconds) {
    if (!spawner || !can_melee_now()) return false;
    const Vector2 target = v_add(aim_at, v_scl(p_vel, lead_seconds));          // swing where they'll be
    const float angle = std::atan2(target.y - ai_position.y, target.x - ai_position.x);
    auto swing = std::make_unique<meele_attack>(tune::MELEE_DAMAGE, 0.0, (double)tune::MELEE_SIZE, MAROON, RED);
    swing->initilize(ai_position, angle);
    (*spawner)(std::move(swing));
    melee_cd = tune::MELEE_COOLDOWN;
    attack_lock = tune::ATTACK_LOCK;
    pending_event_reward += tune::R_MELEE_SWING;
    return true;
}

bool ai_combat_system::fire_ranged(Vector2 aim_at, float noise_degrees) {
    if (!spawner || !can_shoot_now()) return false;
    const float angle = std::atan2(aim_at.y - ai_position.y, aim_at.x - ai_position.x)
                      + gauss(noise_degrees * aim_noise_scale) * tune::PI_F / 180.0f;               // humans aren't perfect
    auto arrow = std::make_unique<ranged_attack>(tune::ARROW_DAMAGE, tune::ARROW_SPEED, PURPLE);
    arrow->initilize(ai_position, angle);
    (*spawner)(std::move(arrow));
    --arrow_count;
    ranged_cd = tune::RANGED_COOLDOWN;
    attack_lock = tune::ATTACK_LOCK;
    pending_event_reward += tune::R_ARROW_FIRED;
    return true;
}

// ---------------------------------------------------------------------------
//  A. closing the gap
// ---------------------------------------------------------------------------
ai_combat_system::steer_cmd ai_combat_system::st_APPROACH() {            // walk in, ease off near melee spacing
    steer_cmd c;
    const float ease = clampf((dist - tune::MELEE_IDEAL) / 140.0f, 0.5f, 1.0f);
    c.vel = v_scl(to_p, tune::SPEED_WALK * ease);
    c.finished = dist <= tune::MELEE_IDEAL + 8.0f;
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_APPROACH_ZIGZAG() {     // walk in with a sway: harder to shoot
    steer_cmd c;
    if (state_time <= 0.0f) aux_f[0] = rand_range(0.0f, 2.0f * tune::PI_F);
    const float sway = std::tanh(2.5f * std::sin(state_time * 2.0f * tune::PI_F / 0.75f + aux_f[0]));
    c.vel = v_add(v_scl(to_p, tune::SPEED_WALK * 0.8f), v_scl(v_perp(to_p), tune::SPEED_WALK * 0.7f * sway));
    c.finished = dist <= tune::MELEE_IDEAL + 40.0f;
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_APPROACH_FLANK() {      // spiral in around the player's side
    steer_cmd c;
    if (state_time <= 0.0f) state_sign = freer_side(to_p);               // go around the side with more room
    const float k = clampf((dist - 110.0f) / 450.0f, 0.25f, 0.85f);      // more sideways when far, more inward when near
    const Vector2 dir = v_norm(v_add(v_scl(to_p, 1.0f - k), v_scl(tangent(state_sign), k)), to_p);
    c.vel = v_scl(dir, tune::SPEED_WALK);
    c.finished = dist <= tune::MELEE_IDEAL + 20.0f;
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_INTERCEPT() {           // lead pursuit: run to where they'll be
    steer_cmd c;
    const float spd = tune::SPEED_WALK * 1.2f;
    const Vector2 target = predicted_player(clampf(dist / spd, 0.0f, 1.1f));
    c.vel = v_scl(v_norm(v_sub(target, ai_position), to_p), spd);
    c.finished = dist <= tune::MELEE_IDEAL + 10.0f;
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_SPRINT_IN() {           // committed dive; swings on arrival
    steer_cmd c;
    const float aim_time = 0.10f;
    if (state_time < aim_time) {                                         // short wind-up while it tracks
        c.vel = v_scl(to_p, tune::SPEED_SPRINT * state_time / aim_time);
    } else {
        if (!aux_locked) {                                               // lock the line: a real commit can be side-stepped
            aux_pos = v_norm(v_sub(predicted_player(0.15f), ai_position), to_p);
            aux_locked = true;
        }
        c.vel = v_scl(aux_pos, tune::SPEED_DASH * 0.85f);
        c.accel = tune::ACCEL_DASH;
        if (state_swings == 0 && dist <= tune::MELEE_TRIGGER && fire_melee(p_pos, 0.06f)) {
            state_swings = 1; state_swing_time = state_time;
        }
    }
    if (state_swings > 0 && state_time - state_swing_time > 0.12f) c.finished = true;
    if (state_swings == 0 && state_time >= aim_time && dist <= tune::MELEE_IDEAL) c.finished = true;
    return c;
}

// ---------------------------------------------------------------------------
//  B. making space
// ---------------------------------------------------------------------------
ai_combat_system::steer_cmd ai_combat_system::st_RETREAT() {             // back off straight, human-like surge/hesitate
    steer_cmd c;
    c.vel = natural_retreat_step(away_dir(), tune::SPEED_WALK, state_time);
    c.finished = dist >= 430.0f;
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_RETREAT_ARC() {         // back off on a diagonal, toward open floor
    steer_cmd c;
    if (state_time <= 0.0f) state_sign = freer_side(away_dir());
    c.vel = natural_retreat_step(v_rot(away_dir(), (float)state_sign * 0.75f), tune::SPEED_WALK, state_time);
    c.finished = dist >= 400.0f;
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_BACKSTEP() {            // quick hop backwards (spacing reset)
    steer_cmd c;
    if (state_time <= 0.0f) state_sign = freer_side(away_dir());
    const float k = clampf(1.0f - state_time / state_duration, 0.0f, 1.0f);
    c.vel = v_scl(v_rot(away_dir(), (float)state_sign * 0.3f), tune::SPEED_DASH * k);
    c.accel = tune::ACCEL_DASH;
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_SPRINT_AWAY() {         // all-out disengage, bends toward open space
    steer_cmd c;
    const Vector2 center = V2(arena.x + arena.width * 0.5f, arena.y + arena.height * 0.5f);
    const float wall = clampf(1.0f - edge_distance(ai_position) / 220.0f, 0.0f, 1.0f);   // near a wall? curve to the middle
    const Vector2 dir = v_norm(v_add(away_dir(), v_scl(v_norm(v_sub(center, ai_position)), 0.2f + 0.9f * wall)), away_dir());
    c.vel = v_scl(dir, tune::SPEED_SPRINT);
    c.accel = tune::ACCEL * 1.5f;
    c.finished = dist > 650.0f;
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_KITE_TO_RANGE() {       // open the gap to bow range, then hold it
    steer_cmd c;
    if (state_time <= 0.0f) aux_f[0] = rand_range(300.0f, 420.0f);
    const float err = dist - aux_f[0];
    if (err < -25.0f) {
        c.vel = natural_retreat_step(away_dir(), tune::SPEED_WALK, state_time);
        aux_f[1] = 0.0f;
    } else if (err > 60.0f) {
        c.vel = v_scl(to_p, tune::SPEED_WALK * 0.6f);
        aux_f[1] = 0.0f;
    } else {                                                             // in the band: small orbit, then done
        c.vel = v_scl(tangent(state_sign), tune::SPEED_WALK * 0.45f);
        if (aux_f[1] <= 0.0f) aux_f[1] = state_time + 0.001f;
        if (state_time - aux_f[1] > 0.25f) c.finished = true;
    }
    return c;
}

// ---------------------------------------------------------------------------
//  C. lateral / evasive
// ---------------------------------------------------------------------------
// orbit the player holding the radius we had when the state began
ai_combat_system::steer_cmd ai_combat_system::strafe_motion(int sign, float speed_scale) {
    steer_cmd c;
    if (!aux_locked) { aux_f[0] = clampf(dist, 90.0f, 420.0f); aux_locked = true; }
    const Vector2 radial = v_scl(to_p, clampf((dist - aux_f[0]) / 60.0f, -1.0f, 1.0f));
    const Vector2 tan_dir = tangent(sign);
    c.vel = v_scl(v_norm(v_add(tan_dir, v_scl(radial, 0.7f)), tan_dir), tune::SPEED_WALK * speed_scale);
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_STRAFE_CW()  { return strafe_motion( 1, 1.0f); }
ai_combat_system::steer_cmd ai_combat_system::st_STRAFE_CCW() { return strafe_motion(-1, 1.0f); }

ai_combat_system::steer_cmd ai_combat_system::st_STRAFE_WEAVE() {        // A-D-A-D: flips direction, slows at each flip
    if (state_time <= 0.0f) aux_f[1] = rand_range(0.28f, 0.45f);
    const float seg = aux_f[1];
    const int idx = (int)(state_time / seg);
    const float frac = state_time / seg - (float)idx;
    const int sign = state_sign * ((idx % 2) ? -1 : 1);
    return strafe_motion(sign, 0.4f + 0.6f * std::sin(tune::PI_F * frac));
}

ai_combat_system::steer_cmd ai_combat_system::st_DODGE_SIDESTEP() {      // dash out of an arrow's flight line
    steer_cmd c;
    if (state_time <= 0.0f) {
        aux_pos = (v_len(dodge_dir) > 0.1f) ? dodge_dir : tangent(freer_side(to_p));
    }
    const float k = clampf(1.0f - state_time / state_duration, 0.0f, 1.0f);
    c.vel = v_scl(aux_pos, tune::SPEED_DASH * k);
    c.accel = tune::ACCEL_DASH;
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_JUKE() {                // fake one way, snap the other
    steer_cmd c;
    if (state_time < 0.20f) c.vel = v_scl(tangent(state_sign), tune::SPEED_SPRINT);
    else c.vel = v_add(v_scl(tangent(-state_sign), tune::SPEED_SPRINT * 1.1f), v_scl(to_p, 40.0f));
    c.accel = tune::ACCEL_DASH;
    return c;
}

// ---------------------------------------------------------------------------
//  D. melee
// ---------------------------------------------------------------------------
ai_combat_system::steer_cmd ai_combat_system::st_MELEE_STRIKE() {        // step into range, swing, follow through
    steer_cmd c;
    if (state_swings == 0) {
        if (dist > tune::MELEE_IDEAL + 4.0f)
            c.vel = v_scl(to_p, tune::SPEED_WALK * 1.1f * clampf((dist - tune::MELEE_IDEAL) / 100.0f, 0.3f, 1.0f));
        if (state_time >= 0.08f && dist <= tune::MELEE_TRIGGER && fire_melee(p_pos, 0.08f)) {
            state_swings = 1; state_swing_time = state_time;
        }
        if (state_time > 0.7f) c.finished = true;                        // never got a chance
    } else {
        c.vel = v_scl(to_p, 40.0f);                                      // small lunge during the swing
        if (state_time - state_swing_time > 0.20f) c.finished = true;
    }
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_MELEE_COMBO() {         // pressure: stay on top of them, swing every time it's ready
    steer_cmd c;
    c.vel = v_scl(to_p, tune::SPEED_WALK * clampf((dist - 60.0f) / 80.0f, 0.0f, 1.2f));
    if (state_time >= 0.08f && dist <= tune::MELEE_TRIGGER && fire_melee(p_pos, 0.08f)) {
        ++state_swings; state_swing_time = state_time;
    }
    if (state_swings >= 2 && state_time - state_swing_time > 0.25f) c.finished = true;
    if (state_swings == 1 && state_time - state_swing_time > 1.2f) c.finished = true;
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_HIT_AND_RUN() {         // swing, then hop out before the counter
    steer_cmd c;
    if (state_swings == 0) {
        c.vel = v_scl(to_p, tune::SPEED_SPRINT);
        if (dist <= tune::MELEE_TRIGGER && fire_melee(p_pos, 0.06f)) { state_swings = 1; state_swing_time = state_time; }
        else if (state_time > 0.5f) c.finished = true;                   // couldn't catch them
    } else {
        const float since = state_time - state_swing_time;
        if (since >= 0.06f && since < 0.30f) {
            c.vel = v_scl(away_dir(), tune::SPEED_DASH * (1.0f - (since - 0.06f) / 0.24f));
            c.accel = tune::ACCEL_DASH;
        } else if (since >= 0.30f) {
            c.vel = natural_retreat_step(away_dir(), tune::SPEED_WALK, state_time);
        }
        if (since > 0.6f) c.finished = true;
    }
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_PUNISH_WHIFF() {        // they just swung and missed: dive on the recovery
    steer_cmd c;
    c.vel = v_scl(v_norm(v_sub(predicted_player(0.10f), ai_position), to_p), tune::SPEED_SPRINT * 1.15f);
    c.accel = tune::ACCEL * 2.0f;
    if (dist <= tune::MELEE_IDEAL) c.vel = V2(0.0f, 0.0f);
    if (state_swings == 0 && dist <= tune::MELEE_TRIGGER && fire_melee(p_pos, 0.06f)) {   // no aim delay: it's a reflex
        state_swings = 1; state_swing_time = state_time;
    }
    if (state_swings > 0 && state_time - state_swing_time > 0.15f) c.finished = true;
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_COUNTER_STANCE() {      // wait at ~melee reach; punish the first mistake
    steer_cmd c;
    const Vector2 hold = v_scl(to_p, clampf((dist - 108.0f) / 50.0f, -1.0f, 1.0f) * tune::SPEED_WALK * 0.5f);
    c.vel = v_add(hold, v_scl(tangent(state_sign), 25.0f * std::sin(state_time * 5.0f)));
    const bool stepped_in = dist <= tune::MELEE_TRIGGER - 6.0f;
    const bool whiffed = !player_melee_active && since_player_melee > 0.18f && since_player_melee < 0.7f && dist <= 150.0f;
    if (state_swings == 0) {
        if (stepped_in && fire_melee(p_pos, 0.08f)) { state_swings = 1; state_swing_time = state_time; }
        else if (whiffed) {
            c.vel = v_scl(to_p, tune::SPEED_SPRINT);
            if (dist <= tune::MELEE_TRIGGER && fire_melee(p_pos, 0.06f)) { state_swings = 1; state_swing_time = state_time; }
        }
    } else if (state_time - state_swing_time > 0.18f) {
        c.finished = true;
    }
    return c;
}

// ---------------------------------------------------------------------------
//  E. ranged
// ---------------------------------------------------------------------------
ai_combat_system::steer_cmd ai_combat_system::st_SHOOT() {               // plant, aim at where they ARE, fire
    steer_cmd c;
    if (state_time <= 0.0f) aux_f[0] = rand_range(0.16f, 0.26f);         // human aim delay
    if (state_swings == 0 && state_time >= aux_f[0]) {
        if (fire_ranged(p_pos, 3.0f)) { state_swings = 1; state_swing_time = state_time; }
        else if (state_time > 0.7f) c.finished = true;
    }
    if (state_swings > 0 && state_time - state_swing_time > 0.22f) c.finished = true;
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_SHOOT_LEAD() {          // plant, aim at where they WILL be
    steer_cmd c;
    if (state_time <= 0.0f) aux_f[0] = rand_range(0.20f, 0.30f);
    if (state_swings == 0 && state_time >= aux_f[0]) {
        const Vector2 aim = predicted_player((dist / (float)tune::ARROW_SPEED) * 0.85f);   // humans slightly under-lead
        if (fire_ranged(aim, 2.0f)) { state_swings = 1; state_swing_time = state_time; }
        else if (state_time > 0.7f) c.finished = true;
    }
    if (state_swings > 0 && state_time - state_swing_time > 0.22f) c.finished = true;
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_SHOOT_STRAFE() {        // move-and-shoot: orbit, fire a led shot mid-orbit
    steer_cmd c = strafe_motion(state_sign, 0.9f);
    if (state_swings == 0 && state_time >= 0.22f) {
        const Vector2 aim = predicted_player((dist / (float)tune::ARROW_SPEED) * 0.85f);
        if (fire_ranged(aim, 3.0f)) { state_swings = 1; state_swing_time = state_time; }
    }
    return c;                                                            // runs its full duration: keeps moving after the shot
}

ai_combat_system::steer_cmd ai_combat_system::st_SHOOT_RETREAT() {       // fire, then back away (kiting)
    steer_cmd c;
    if (state_swings == 0) {
        if (state_time >= 0.15f) {
            const Vector2 aim = predicted_player((dist / (float)tune::ARROW_SPEED) * 0.85f);
            if (fire_ranged(aim, 2.5f)) { state_swings = 1; state_swing_time = state_time; }
        }
        if (state_time > 0.7f) c.finished = true;
    } else {
        c.vel = natural_retreat_step(away_dir(), tune::SPEED_WALK, state_time);
    }
    return c;
}

// ---------------------------------------------------------------------------
//  F. resources
// ---------------------------------------------------------------------------
// walk to aux_pos; done on arrival or when someone else took the arrow
ai_combat_system::steer_cmd ai_combat_system::grab_motion(float speed) {
    steer_cmd c;
    bool still_there = false;
    for (const Vector2& a : arrow_positions) if (v_dist(a, aux_pos) < 25.0f) still_there = true;
    if (!still_there) { c.finished = true; return c; }
    c.vel = v_scl(v_norm(v_sub(route_around(aux_pos), ai_position)), speed);
    if (v_dist(ai_position, aux_pos) < 8.0f) c.finished = true;
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_GRAB_ARROW() {          // nearest arrow
    if (state_time <= 0.0f) {
        if (!arrow_exists) { steer_cmd c; c.finished = true; return c; }
        aux_pos = nearest_arrow;
        state_duration = clampf(v_dist(ai_position, aux_pos) / (tune::SPEED_WALK * 1.1f) + 0.5f, 0.8f, 3.5f);   // enough time to get there
    }
    return grab_motion(tune::SPEED_WALK * 1.1f);
}

ai_combat_system::steer_cmd ai_combat_system::st_GRAB_ARROW_SAFE() {     // the arrow I can reach before the player does
    if (state_time <= 0.0f) {
        if (!arrow_exists) { steer_cmd c; c.finished = true; return c; }
        float best = -1e9f;
        for (const Vector2& a : arrow_positions) {
            const float mine = v_dist(a, ai_position);
            const float score = v_dist(a, p_pos) - 1.15f * mine;         // far from them, close to me
            if (score > best) { best = score; aux_pos = a; }
        }
        state_duration = clampf(v_dist(ai_position, aux_pos) / (tune::SPEED_WALK * 1.1f) + 0.5f, 0.8f, 3.5f);
    }
    return grab_motion(tune::SPEED_WALK * 1.1f);
}

// ---------------------------------------------------------------------------
//  G. mind games
// ---------------------------------------------------------------------------
ai_combat_system::steer_cmd ai_combat_system::st_BAIT_PLAYER() {         // dance at the edge of their reach to draw a swing
    steer_cmd c;
    const float phase = std::fmod(state_time, 0.8f);
    if (phase < 0.30f) { if (dist > 112.0f) c.vel = v_scl(to_p, tune::SPEED_SPRINT); }
    else if (dist < 170.0f) c.vel = natural_retreat_step(away_dir(), tune::SPEED_WALK * 0.9f, state_time);
    if (player_melee_active && state_time > 0.12f) c.finished = true;    // they bit: next decision can punish
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_FAKE_RETREAT() {        // look scared, then spring the ambush
    steer_cmd c;
    if (state_time < 0.4f * state_duration) {
        c.vel = natural_retreat_step(away_dir(), tune::SPEED_WALK, state_time);
    } else {
        c.vel = v_scl(v_norm(v_sub(predicted_player(0.10f), ai_position), to_p), tune::SPEED_SPRINT * 1.15f);
        c.accel = tune::ACCEL * 2.0f;
        if (dist <= tune::MELEE_IDEAL) c.vel = V2(0.0f, 0.0f);
        if (state_swings == 0 && dist <= tune::MELEE_TRIGGER && fire_melee(p_pos, 0.06f)) {
            state_swings = 1; state_swing_time = state_time;
        }
        if (state_swings > 0 && state_time - state_swing_time > 0.15f) c.finished = true;
    }
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_FAKE_CHARGE() {         // rush, stop dead, slip sideways: provokes a panic swing/shot
    steer_cmd c;
    if (state_time < 0.38f && dist > 170.0f) {
        c.vel = v_scl(to_p, tune::SPEED_SPRINT);
    } else if (state_time < 0.70f) {
        if (!aux_locked) { state_sign = freer_side(to_p); aux_locked = true; aux_f[0] = state_time; }
        const float k = clampf(1.0f - (state_time - aux_f[0]) / 0.30f, 0.0f, 1.0f);
        c.vel = v_scl(tangent(state_sign), tune::SPEED_DASH * 0.8f * k);
        c.accel = tune::ACCEL_DASH;
    }
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_HOLD_GROUND() {         // stand and read the player
    steer_cmd c;
    c.vel = v_scl(tangent(state_sign), 10.0f * std::sin(state_time * 4.0f));
    return c;
}

// ---------------------------------------------------------------------------
//  H. map positioning
// ---------------------------------------------------------------------------
ai_combat_system::steer_cmd ai_combat_system::st_REPOSITION_OPEN() {     // take the middle / get out of corners
    steer_cmd c;
    const Vector2 center = V2(arena.x + arena.width * 0.5f, arena.y + arena.height * 0.5f);
    const Vector2 target = v_add(center, v_scl(v_sub(center, p_pos), 0.10f));   // slightly on my side of the middle
    c.vel = v_scl(v_norm(v_sub(target, ai_position)), tune::SPEED_WALK);
    c.finished = v_dist(ai_position, target) < 70.0f;
    return c;
}

ai_combat_system::steer_cmd ai_combat_system::st_MOVE_BEHIND_COVER() {   // get a wall between me and the player
    steer_cmd c;
    const Vector2 prefer = aux_pos;
    if (!pick_cover_point(aux_pos, aux_locked ? &prefer : nullptr)) { c.finished = true; return c; }   // re-picked each tick: tracks the player
    aux_locked = true;
    float len = 0.0f;
    const Vector2 goal = route_around(aux_pos, &len);
    if (state_time <= 0.0f)                                              // enough time to get there (+ a moment hiding)
        state_duration = clampf(len / (tune::SPEED_WALK * 1.15f) * 1.25f + 0.7f, 1.0f, 3.0f);
    const float d = v_dist(ai_position, aux_pos);
    if (d > 12.0f)
        c.vel = v_scl(v_norm(v_sub(goal, ai_position)), tune::SPEED_WALK * 1.15f * clampf(d / 60.0f, 0.35f, 1.0f));
    return c;
}

// ============================================================================
//  9. LEARNING  -  Double + Dueling DQN over *states* (semi-MDP, masked)
// ============================================================================
// epsilon-greedy, but only ever over the states that are legal right now
int64_t ai_combat_system::choose_action(torch::Tensor state) {
    const uint32_t mask = current_valid_mask ? current_valid_mask : 0xFFFFFFFFu;

    if (rand01() < std::max(epsilon, mistake_rate)) {
        int valid[32];
        int n = 0;
        for (int i = 0; i < ACTION_COUNT; ++i) if (mask & (1u << i)) valid[n++] = i;
        return valid[(int)(rand01() * (float)n) % n];
    }

    torch::NoGradGuard no_grad;
    torch::Tensor q = model->forward(state).contiguous();
    const float* qd = q.data_ptr<float>();
    int best = -1;
    float best_q = -std::numeric_limits<float>::infinity();
    for (int i = 0; i < ACTION_COUNT; ++i) {
        if (!(mask & (1u << i))) continue;
        if (qd[i] > best_q) { best_q = qd[i]; best = i; }
    }
    return best >= 0 ? best : (int)STATE_HOLD_GROUND;
}

void ai_combat_system::push_transition(const std::array<float, STATE_SIZE>& next_state, uint32_t next_mask, bool terminal) {
    step_memory m;
    m.state = pending.state;
    m.next_state = next_state;
    m.next_mask = next_mask;
    m.ret = pending.ret;
    m.disc = terminal ? 0.0f : pending.disc;             // gamma^duration, or 0 if the game really ended
    m.action = pending.action;

    // hits, damage and endings are rare but matter most: keep them in their own buffer and over-sample them
    const bool eventful = terminal || std::fabs(pending.ret) > tune::EVENTFUL_RETURN;
    std::vector<step_memory>& buf = eventful ? replay_event : replay_normal;
    size_t& head = eventful ? event_head : normal_head;
    const size_t cap = eventful ? tune::EVENT_CAPACITY : tune::NORMAL_CAPACITY;
    if (buf.size() < cap) buf.push_back(m);
    else { buf[head] = m; head = (head + 1) % cap; }
}

void ai_combat_system::train_step() {
    const size_t n_norm = replay_normal.size(), n_evt = replay_event.size();
    if (n_norm + n_evt < (size_t)tune::WARMUP_TRANSITIONS) return;
    const int B = tune::BATCH_SIZE;

    for (int b = 0; b < B; ++b) {
        const bool use_evt = n_evt > 0 && (n_norm == 0 || rand01() < tune::EVENT_FRACTION);
        const step_memory& m = use_evt ? replay_event [(size_t)(rand01() * (float)n_evt)  % n_evt]
                                       : replay_normal[(size_t)(rand01() * (float)n_norm) % n_norm];
        std::copy(m.state.begin(),      m.state.end(),      sb_state.begin() + (size_t)b * STATE_SIZE);
        std::copy(m.next_state.begin(), m.next_state.end(), sb_next.begin()  + (size_t)b * STATE_SIZE);
        sb_action[b] = m.action;
        sb_ret[b] = m.ret;
        sb_disc[b] = m.disc;
        for (int a = 0; a < ACTION_COUNT; ++a)
            sb_mask[(size_t)b * ACTION_COUNT + a] = ((m.next_mask >> a) & 1u) ? 1.0f : 0.0f;
    }

    const auto f32 = torch::kFloat32;
    torch::Tensor t_s  = torch::from_blob(sb_state.data(),  {B, STATE_SIZE},   f32);
    torch::Tensor t_s2 = torch::from_blob(sb_next.data(),   {B, STATE_SIZE},   f32);
    torch::Tensor t_a  = torch::from_blob(sb_action.data(), {B, 1},            torch::kInt64);
    torch::Tensor t_r  = torch::from_blob(sb_ret.data(),    {B, 1},            f32);
    torch::Tensor t_d  = torch::from_blob(sb_disc.data(),   {B, 1},            f32);
    torch::Tensor t_m  = torch::from_blob(sb_mask.data(),   {B, ACTION_COUNT}, f32);

    torch::Tensor q = model->forward(t_s).gather(1, t_a);
    torch::Tensor target;
    {
        torch::NoGradGuard no_grad;
        // Double DQN, restricted to states that are legal in s': online net picks, target net scores
        torch::Tensor online_next = model->forward(t_s2).masked_fill(t_m.lt(0.5), -1e9);
        torch::Tensor best = online_next.argmax(1, true);
        torch::Tensor q_next = target_model->forward(t_s2).gather(1, best);
        target = t_r + t_d * q_next;                     // t_d = gamma^duration (0 on terminal)
    }

    torch::Tensor loss = torch::smooth_l1_loss(q, target);
    const float lv = loss.item<float>();
    if (!std::isfinite(lv)) { std::printf("[ai] non-finite loss, step skipped\n"); return; }

    optimizer->zero_grad();
    loss.backward();
    torch::nn::utils::clip_grad_norm_(model->parameters(), tune::GRAD_CLIP);
    optimizer->step();
    soft_update_target();

    ++train_steps;
    avg_loss = (train_steps == 1) ? lv : 0.99f * avg_loss + 0.01f * lv;
    if (debug && train_steps % 2000 == 0) {
        std::printf("[ai] step %lld | eps %.3f | loss %.4f | replay %zu+%zu | decisions %lld\n",
                    (long long)train_steps, epsilon, avg_loss, n_norm, n_evt, (long long)decisions_total);
    }
}

// a few extra gradient steps at the end of every round (Game::end_round calls this)
void ai_combat_system::train_on_history() {
    if (!learning_enabled) return;
    for (int i = 0; i < tune::EPISODE_END_TRAIN_STEPS; ++i) train_step();
}

void ai_combat_system::save_model(const std::string& path) {
    try {
        torch::save(model, path);
        std::ofstream meta(path + ".meta");               // so a reload keeps its exploration level
        meta << epsilon << " " << decisions_total << " " << train_steps << "\n";
        std::printf("[ai] saved %s (eps %.3f, %lld steps)\n", path.c_str(), epsilon, (long long)train_steps);
    } catch (const std::exception& e) {
        std::printf("[ai] could not save %s: %s\n", path.c_str(), e.what());
    }
}

void ai_combat_system::load_model(const std::string& path) {
    {
        std::ifstream probe(path, std::ios::binary);
        if (!probe.good()) { std::printf("[ai] no %s yet - starting fresh\n", path.c_str()); return; }
    }
    try {
        torch::load(model, path);
        hard_sync_target();
        std::ifstream meta(path + ".meta");
        float e = 0.10f; long long d = 0, t = 0;
        if (meta >> e >> d >> t) { decisions_total = d; train_steps = t; }
        epsilon = clampf(e, tune::EPS_MIN, 1.0f);
        std::printf("[ai] loaded %s (eps %.3f, %lld steps)\n", path.c_str(), epsilon, (long long)train_steps);
    } catch (const std::exception& e) {
        // e.g. an old 10-action model.pt: different shapes. Start clean rather than half-loaded.
        std::printf("[ai] could not load %s (%s) - starting fresh\n", path.c_str(), e.what());
        build_networks();
    }
}

// ============================================================================
//  10. DRAW
// ============================================================================
void ai_combat_system::draw() {
    const Vector2 size = V2(40.0f, 40.0f);
    DrawRectangleV(V2(ai_position.x - 20.0f, ai_position.y - 20.0f), size, WHITE);
    DrawRectangleLines((int)(ai_position.x - 20.0f), (int)(ai_position.y - 20.0f), 40, 40, BLACK);


    const int x = (int)ai_position.x - 20, y = (int)ai_position.y - 36;
    const float hp = clampf((float)ai_hp / (float)ai_max_hp, 0.0f, 1.0f);

    DrawRectangle(x, y, 40, 5, DARKGRAY);
    DrawRectangle(x, y, (int)(40.0f * hp), 5, hp > 0.35f ? GREEN : RED);

    if (!debug) return;
    DrawText(get_state_name(), x - 20, y - 16, 14, DARKGRAY);
    DrawRectangle(x, y + 7, 12, 4, melee_cd <= 0.0f ? MAROON : LIGHTGRAY);                 // melee ready
    DrawRectangle(x + 14, y + 7, 12, 4, (arrow_count > 0 && ranged_cd <= 0.0f) ? PURPLE : LIGHTGRAY);   // bow ready
    DrawText(TextFormat("%d", arrow_count), x + 29, y + 4, 10, DARKGRAY);                  // arrows held
}

// ============================================================================
//  11. SPARRING BOT  - a scripted opponent for fast training (drives the Player object)
//  Without it the AI trains against a statue and only ever learns "walk up and swing".
//  Five personalities, re-rolled every episode, so the policy has to generalise.
// ============================================================================
float g_bot_skill_override = -1.0f;   // < 0: random skill each episode

namespace {
constexpr float BOT_ARENA_W = 1920.0f, BOT_ARENA_H = 1080.0f, BOT_BODY = 20.0f;
}

void sparring_bot::begin_episode(const Player& player) {
    const float r = rand01();
    arch = r < 0.12f ? DUMMY : r < 0.40f ? RUSHER : r < 0.60f ? KITER : r < 0.75f ? TURTLE : STRAFER;
    // skill 0 = sloppy (slow to react, bad aim, rarely dodges), 1 = sharp. Random per episode so the AI
    // meets the whole range instead of overfitting to one opponent.
    skill = (g_bot_skill_override >= 0.0f) ? g_bot_skill_override : rand01();
    speed = 165.0f + 50.0f * rand01() + 25.0f * skill;
    reaction = (0.40f + (0.04f - 0.40f) * skill) + 0.05f * rand01();      // time the AI must linger in reach before the bot swings
    dodge_chance = 0.25f + (0.92f - 0.25f) * skill;                       // vs incoming arrows
    melee_dodge  = 0.50f * skill;                                         // vs incoming swings
    swing_cd = 1.5f + (1.0f - 1.5f) * skill;
    aim_error = 0.14f + (0.03f - 0.14f) * skill;
    anticipate = skill > 0.45f;
    last_dodge_roll = nullptr;
    in_range_time = 0.0f;
    melee_cd = ranged_cd = lock = dodge_time = 0.0f;
    ammo = 0;
    last_seen_amount = player.get_ranged_amount();
    strafe_sign = rand01() < 0.5f ? -1 : 1;
    strafe_flip = 1.0f + 2.0f * rand01();
    lunge_timer = 1.5f + 2.0f * rand01();
    dodge_vec = V2(0.0f, 0.0f);
}

void sparring_bot::update(float dt, Player& player, const ai_combat_system& ai, const attack_list& ai_attacks,
                          const attack_spawner& spawn_player_attack) {
    melee_cd = std::max(0.0f, melee_cd - dt);
    ranged_cd = std::max(0.0f, ranged_cd - dt);
    lock = std::max(0.0f, lock - dt);
    dodge_time = std::max(0.0f, dodge_time - dt);

    const Vector2 me = player.get_pos();
    const Vector2 foe = ai.get_position();
    const float dist = v_dist(me, foe);
    const Vector2 to = v_norm(v_sub(foe, me), V2(-1.0f, 0.0f));
    const Vector2 perp = v_perp(to);

    const int amount = player.get_ranged_amount();       // arrows picked up by the arrow handler
    if (amount > last_seen_amount) ammo += amount - last_seen_amount;
    last_seen_amount = amount;

    // reflex: an AI arrow is about to hit me -> sometimes sidestep
    if (dodge_time <= 0.0f && arch != DUMMY) {
        for (const auto& up : ai_attacks) {
            if (!up || !up->is_Alive() || !dynamic_cast<const ranged_attack*>(up.get())) continue;
            const Vector2 ad = up->get_dir();
            const Vector2 rel = v_sub(me, up->get_pos());
            const float along = v_dot(rel, ad), lat = v_crs(ad, rel);
            if (along > 0.0f && along < 330.0f && std::fabs(lat) < 40.0f && rand01() < dodge_chance) {
                dodge_time = 0.28f;
                dodge_vec = v_scl(v_perp(ad), lat >= 0.0f ? 1.0f : -1.0f);
                break;
            }
        }
    }

    // reflex: an AI swing is coming at me -> sharp bots sometimes sidestep it (one roll per swing)
    if (dodge_time <= 0.0f && arch != DUMMY && melee_dodge > 0.0f) {
        for (const auto& up : ai_attacks) {
            if (!up || !up->is_Alive() || dynamic_cast<const ranged_attack*>(up.get())) continue;
            if (up.get() == last_dodge_roll) continue;
            last_dodge_roll = up.get();
            if (dist < 150.0f && rand01() < melee_dodge) {
                dodge_time = 0.28f;
                dodge_vec = v_scl(perp, rand01() < 0.5f ? 1.0f : -1.0f);
            }
        }
    }

    // ---- movement ----------------------------------------------------------
    Vector2 vel = V2(0.0f, 0.0f);
    if (dodge_time > 0.0f) {
        vel = v_scl(dodge_vec, 330.0f);
    } else {
        switch (arch) {
            case DUMMY:
            case TURTLE: break;                                              // stands its ground
            case RUSHER: if (dist > 70.0f) vel = v_scl(to, speed); break;
            case KITER: {
                const auto& arrows = ai.get_known_arrows();
                if (ammo == 0 && !arrows.empty()) {                          // out of ammo: go shopping
                    Vector2 best = arrows[0];
                    for (const Vector2& a : arrows) if (v_dist(a, me) < v_dist(best, me)) best = a;
                    vel = v_scl(v_norm(v_sub(best, me)), speed);
                } else if (dist < 300.0f) vel = v_scl(to, -speed);
                else if (dist > 440.0f)   vel = v_scl(to, speed * 0.7f);
                else                      vel = v_scl(perp, (float)strafe_sign * speed * 0.6f);
                break;
            }
            case STRAFER: {
                strafe_flip -= dt;
                if (strafe_flip <= 0.0f) { strafe_sign = -strafe_sign; strafe_flip = 1.0f + 2.0f * rand01(); }
                lunge_timer -= dt;
                if (lunge_timer <= 0.0f && dist > 80.0f) vel = v_scl(to, speed * 1.3f);   // dash in to trade
                else {
                    if (lunge_timer <= 0.0f) lunge_timer = 2.0f + 2.0f * rand01();
                    vel = v_add(v_scl(perp, (float)strafe_sign * speed * 0.8f),
                                v_scl(to, clampf((dist - 170.0f) / 60.0f, -1.0f, 1.0f) * speed * 0.5f));
                }
                break;
            }
        }
    }
    if (dist < 50.0f && v_dot(vel, to) > 0.0f) vel = v_sub(vel, v_scl(to, v_dot(vel, to)));   // no walking through the AI
    Vector2 np = v_add(me, v_scl(vel, dt));
    np.x = clampf(np.x, BOT_BODY, BOT_ARENA_W - BOT_BODY);
    np.y = clampf(np.y, BOT_BODY, BOT_ARENA_H - BOT_BODY);
    player.set_position(np);

    // ---- attacks -----------------------------------------------------------
    const Vector2 foe_v = ai.get_velocity();
    const float closing = -v_dot(foe_v, to);                                  // > 0: the AI is moving toward me
    const bool diving = anticipate && dist <= 100.0f && closing > 120.0f;     // sharp bots don't wait for a hit
    const float lead_t = skill > 0.6f ? 0.08f : 0.0f;
    const float ang = std::atan2(foe.y + foe_v.y * lead_t - np.y, foe.x + foe_v.x * lead_t - np.x);
    in_range_time = (dist <= 88.0f) ? in_range_time + dt : 0.0f;
    if (diving) in_range_time = std::max(in_range_time, reaction);
    if (arch != DUMMY && in_range_time >= reaction && melee_cd <= 0.0f && lock <= 0.0f) {
        auto swing = std::make_unique<meele_attack>(10.0, 5.0, 60.0, YELLOW, DARKBROWN);
        swing->set_owner_body_extent(player.get_body_extent());
        swing->initilize(np, ang);
        spawn_player_attack(std::move(swing));
        melee_cd = swing_cd + 0.3f * rand01();
        lock = 0.30f;
    }
    const bool shooter = (arch == KITER || arch == TURTLE || arch == RUSHER);
    if (shooter && ammo > 0 && ranged_cd <= 0.0f && lock <= 0.0f && dist > 150.0f && dist < 650.0f
        && rand01() < dt * 2.0f) {                                           // ~0.5 s reaction jitter
        auto arrow = std::make_unique<ranged_attack>(10.0, 400.0, SKYBLUE);
        const float shot_lead = skill > 0.6f ? (dist / 400.0f) * 0.7f : 0.0f; // sharp bots lead their shots
        const float sang = std::atan2(foe.y + foe_v.y * shot_lead - np.y, foe.x + foe_v.x * shot_lead - np.x);
        arrow->initilize(np, sang + (rand01() - 0.5f) * 2.0f * aim_error);
        spawn_player_attack(std::move(arrow));
        --ammo;
        ranged_cd = 1.5f;
        lock = 0.30f;
    }
}