#include "particle.h"
#include <cmath>
#include <cstdlib>

//this section was written by AI, im not writing all that...


particle_system::particle_system(size_t max_particles) {
    init_pool(max_particles);
}

void particle_system::init_pool(size_t max_particles) {
    m_particles.resize(max_particles);
    for (auto& p : m_particles) {
        p.is_alive = false;
    }
}

void particle_system::emit(const particle_config& config, int count) {
    int emitted = 0;
    for (auto& p : m_particles) {
        if (emitted >= count) break;
        if (p.is_alive) continue;

        float speed = config.min_speed + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (config.max_speed - config.min_speed)));
        float angle_deg = config.min_angle + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (config.max_angle - config.min_angle)));
        float angle_rad = angle_deg * (PI / 180.0f);

        p.position = config.origin;
        p.velocity = { std::cos(angle_rad) * speed, std::sin(angle_rad) * speed };
        p.acceleration = config.gravity;

        p.start_size = config.min_start_size + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (config.max_start_size - config.min_start_size)));
        p.current_size = p.start_size;
        p.end_size_multiplier = config.end_size_multiplier;

        p.max_lifetime = config.min_lifetime + static_cast<float>(rand()) / (static_cast<float>(RAND_MAX / (config.max_lifetime - config.min_lifetime)));
        p.lifetime = p.max_lifetime;

        p.start_color = config.start_color;
        p.end_color = config.end_color;
        p.current_color = config.start_color;
        p.fade_alpha = config.fade_alpha;
        
        p.is_alive = true;
        emitted++;
    }
}

void particle_system::update(float dt) {
    for (auto& p : m_particles) {
        if (!p.is_alive) continue;

        p.lifetime -= dt;
        if (p.lifetime <= 0.0f) {
            p.is_alive = false;
            continue;
        }

        p.velocity.x += p.acceleration.x * dt;
        p.velocity.y += p.acceleration.y * dt;
        p.position.x += p.velocity.x * dt;
        p.position.y += p.velocity.y * dt;

        float life_ratio = p.lifetime / p.max_lifetime;
        float progress = 1.0f - life_ratio;

        float target_size = p.start_size * p.end_size_multiplier;
        p.current_size = p.start_size + (target_size - p.start_size) * progress;
        if (p.current_size < 0.0f) p.current_size = 0.0f;

        p.current_color.r = static_cast<unsigned char>(p.start_color.r + (p.end_color.r - p.start_color.r) * progress);
        p.current_color.g = static_cast<unsigned char>(p.start_color.g + (p.end_color.g - p.start_color.g) * progress);
        p.current_color.b = static_cast<unsigned char>(p.start_color.b + (p.end_color.b - p.start_color.b) * progress);
        
        if (p.fade_alpha) {
            p.current_color.a = static_cast<unsigned char>(p.start_color.a * life_ratio);
        } else {
            p.current_color.a = static_cast<unsigned char>(p.start_color.a + (p.end_color.a - p.start_color.a) * progress);
        }
    }
}

void particle_system::draw() const {
    BeginBlendMode(BLEND_ALPHA);
    for (const auto& p : m_particles) {
        if (!p.is_alive) continue;
        DrawCircleV(p.position, p.current_size, p.current_color);
    }
    EndBlendMode();
}

void particle_system::clear() {
    for (auto& p : m_particles) {
        p.is_alive = false;
    }
}

// =========================================================================
// PRESET IMPLEMENTATIONS
// =========================================================================

void particle_system::emit_explosion(Vector2 pos, Color inner_color, Color outer_color, float size_scale, float speed_scale, float duration_scale, int count) {
    particle_config config;
    config.type = particle_type::EXPLOSION;
    config.origin = pos;
    config.min_speed = 60.0f * speed_scale;
    config.max_speed = 250.0f * speed_scale;
    config.min_angle = 0.0f;
    config.max_angle = 360.0f;
    config.min_start_size = 6.0f * size_scale;
    config.max_start_size = 12.0f * size_scale;
    config.end_size_multiplier = 0.1f;
    config.min_lifetime = 0.4f * duration_scale;
    config.max_lifetime = 0.9f * duration_scale;
    config.start_color = inner_color;
    config.end_color = outer_color;
    emit(config, count);
}

void particle_system::emit_bleed(Vector2 pos, Color blood_color, Color dark_color, float size_scale, float speed_scale, float duration_scale, int count) {
    particle_config config;
    config.type = particle_type::BLEED;
    config.origin = pos;
    config.min_speed = 40.0f * speed_scale;
    config.max_speed = 180.0f * speed_scale;
    config.min_angle = 0.0f;
    config.max_angle = 360.0f;
    config.gravity = { 0.0f, 380.0f }; 
    config.min_start_size = 3.0f * size_scale;
    config.max_start_size = 6.0f * size_scale;
    config.end_size_multiplier = 0.3f;
    config.min_lifetime = 0.3f * duration_scale;
    config.max_lifetime = 0.6f * duration_scale;
    config.start_color = blood_color;
    config.end_color = dark_color;
    emit(config, count);
}

void particle_system::emit_smoke(Vector2 pos, Color base_color, Color target_color, float size_scale, float duration_scale, int count) {
    particle_config config;
    config.type = particle_type::SMOKE;
    config.origin = pos;
    config.min_speed = 15.0f;
    config.max_speed = 45.0f;
    config.min_angle = 250.0f; 
    config.max_angle = 290.0f;
    config.gravity = { 0.0f, -20.0f }; 
    config.min_start_size = 8.0f * size_scale;
    config.max_start_size = 16.0f * size_scale;
    config.end_size_multiplier = 1.8f; 
    config.min_lifetime = 0.8f * duration_scale;
    config.max_lifetime = 1.5f * duration_scale;
    config.start_color = base_color;
    config.end_color = target_color;
    emit(config, count);
}

void particle_system::emit_sparks(Vector2 pos, Color flash_color, Color fade_color, float size_scale, float speed_scale, int count) {
    particle_config config;
    config.type = particle_type::SPARK;
    config.origin = pos;
    config.min_speed = 120.0f * speed_scale;
    config.max_speed = 300.0f * speed_scale;
    config.min_angle = 0.0f;
    config.max_angle = 360.0f;
    config.gravity = { 0.0f, 100.0f };
    config.min_start_size = 1.5f * size_scale;
    config.max_start_size = 3.5f * size_scale;
    config.end_size_multiplier = 0.0f;
    config.min_lifetime = 0.15f;
    config.max_lifetime = 0.4f;
    config.start_color = flash_color;
    config.end_color = fade_color;
    emit(config, count);
}

void particle_system::emit_ambient_rain(Vector2 top_left_screen, float width_range, Color rain_color, float speed_scale, int count) {
    particle_config config;
    config.type = particle_type::RAIN;
    config.min_speed = 400.0f * speed_scale;
    config.max_speed = 600.0f * speed_scale;
    config.min_angle = 85.0f;  
    config.max_angle = 95.0f;
    config.min_start_size = 1.0f;
    config.max_start_size = 2.0f;
    config.end_size_multiplier = 1.0f;
    config.min_lifetime = 2.0f;
    config.max_lifetime = 2.5f;
    config.start_color = rain_color;
    config.end_color = rain_color;
    config.fade_alpha = false;

    for (int i = 0; i < count; ++i) {
        float rand_x = top_left_screen.x + static_cast<float>(rand() % static_cast<int>(width_range));
        config.origin = { rand_x, top_left_screen.y };
        emit(config, 1);
    }
}

void particle_system::emit_fire(Vector2 pos, Color core_color, Color tip_color, float size_scale, float speed_scale, int count) {
    particle_config config;
    config.type = particle_type::FIRE;
    config.origin = pos;
    config.min_speed = 30.0f * speed_scale;
    config.max_speed = 90.0f * speed_scale;
    config.min_angle = 240.0f; 
    config.max_angle = 300.0f;
    config.gravity = { 0.0f, -40.0f }; 
    config.min_start_size = 5.0f * size_scale;
    config.max_start_size = 10.0f * size_scale;
    config.end_size_multiplier = 0.2f; 
    config.min_lifetime = 0.3f;
    config.max_lifetime = 0.7f;
    config.start_color = core_color;
    config.end_color = tip_color;
    emit(config, count);
}

void particle_system::emit_magic(Vector2 pos, Color spark_color, Color glow_color, float size_scale, int count) {
    particle_config config;
    config.type = particle_type::MAGIC;
    config.origin = pos;
    config.min_speed = 20.0f;
    config.max_speed = 60.0f;
    config.min_angle = 0.0f;
    config.max_angle = 360.0f;
    config.min_start_size = 2.0f * size_scale;
    config.max_start_size = 4.0f * size_scale;
    config.end_size_multiplier = 1.2f;
    config.min_lifetime = 0.6f;
    config.max_lifetime = 1.1f;
    config.start_color = spark_color;
    config.end_color = glow_color;
    emit(config, count);
}

void particle_system::emit_toxic_gas(Vector2 pos, Color gas_color, Color fade_color, float size_scale, int count) {
    particle_config config;
    config.type = particle_type::TOXIC;
    config.origin = pos;
    config.min_speed = 10.0f;
    config.max_speed = 30.0f;
    config.min_angle = 0.0f;
    config.max_angle = 360.0f;
    config.min_start_size = 10.0f * size_scale;
    config.max_start_size = 20.0f * size_scale;
    config.end_size_multiplier = 2.0f; // Expand significantly to cover ground area
    config.min_lifetime = 1.0f;
    config.max_lifetime = 2.0f;
    config.start_color = gas_color;
    config.end_color = fade_color;
    emit(config, count);
}

void particle_system::emit_electric_burst(Vector2 pos, Color arc_color, Color flash_color, float size_scale, int count) {
    particle_config config;
    config.type = particle_type::ELECTRIC;
    config.origin = pos;
    config.min_speed = 150.0f;
    config.max_speed = 400.0f; // Fast jagged snap movements
    config.min_angle = 0.0f;
    config.max_angle = 360.0f;
    config.min_start_size = 1.0f * size_scale;
    config.max_start_size = 3.0f * size_scale;
    config.end_size_multiplier = 0.0f;
    config.min_lifetime = 0.05f;
    config.max_lifetime = 0.2f; // Short burst lifetime frames
    config.start_color = arc_color;
    config.end_color = flash_color;
    
    emit(config, count);
}
