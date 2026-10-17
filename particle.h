#pragma once
#include "pch.h"
#include <vector>

enum class particle_type {
    CUSTOM,
    EXPLOSION,
    BLEED,
    SMOKE,
    SPARK,
    RAIN,
    FIRE,
    MAGIC,
    TOXIC,
    ELECTRIC
};

struct particle_config {
    particle_type type = particle_type::CUSTOM;
    Vector2 origin = { 0.0f, 0.0f };
    
    float min_speed = 50.0f;
    float max_speed = 150.0f;
    float min_angle = 0.0f;       
    float max_angle = 360.0f;
    Vector2 gravity = { 0.0f, 0.0f };

    float min_start_size = 4.0f;
    float max_start_size = 8.0f;
    float end_size_multiplier = 0.0f; 

    float min_lifetime = 0.5f;
    float max_lifetime = 1.2f;

    Color start_color = GOLD;
    Color end_color = RED;
    bool fade_alpha = true;       
};

struct particle {
    Vector2 position;
    Vector2 velocity;
    Vector2 acceleration;
    
    float start_size;
    float current_size;
    float end_size_multiplier;

    float lifetime;
    float max_lifetime;

    Color start_color;
    Color end_color;
    Color current_color;
    bool fade_alpha;
    bool is_alive;
};

class particle_system {
private:
    std::vector<particle> m_particles;
    void init_pool(size_t max_particles);

public:
    particle_system(size_t max_particles = 5000);
    ~particle_system() = default;

    // Fully custom creation interface
    void emit(const particle_config& config, int count);
    
    void update(float dt);
    void draw() const;
    void clear();

    
    void emit_explosion(Vector2 pos, Color inner_color = GOLD, Color outer_color = RED, 
                        float size_scale = 1.0f, float speed_scale = 1.0f, float duration_scale = 1.0f, int count = 60);

    void emit_bleed(Vector2 pos, Color blood_color = RED, Color dark_color = MAROON, 
                    float size_scale = 1.0f, float speed_scale = 1.0f, float duration_scale = 1.0f, int count = 30);

    void emit_smoke(Vector2 pos, Color base_color = GRAY, Color target_color = DARKGRAY, 
                     float size_scale = 1.0f, float duration_scale = 1.0f, int count = 15);

    void emit_sparks(Vector2 pos, Color flash_color = ORANGE, Color fade_color = YELLOW, 
                      float size_scale = 1.0f, float speed_scale = 1.0f, int count = 25);

    void emit_ambient_rain(Vector2 top_left_screen, float width_range, Color rain_color = BLUE, 
                           float speed_scale = 1.0f, int count = 5);

    void emit_fire(Vector2 pos, Color core_color = ORANGE, Color tip_color = RED, 
                   float size_scale = 1.0f, float speed_scale = 1.0f, int count = 10);

    void emit_magic(Vector2 pos, Color spark_color = PURPLE, Color glow_color = VIOLET, 
                    float size_scale = 1.0f, int count = 20);

    void emit_toxic_gas(Vector2 pos, Color gas_color = LIME, Color fade_color = DARKGREEN, 
                         float size_scale = 1.0f, int count = 8);

    void emit_electric_burst(Vector2 pos, Color arc_color = SKYBLUE, Color flash_color = WHITE, 
                             float size_scale = 1.0f, int count = 12);
};
