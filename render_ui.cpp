#include "render_ui.h"

#include "pch.h"

#include <algorithm>
#include <memory>

extern bool in_game;
 
namespace {

// move every live attack; swings follow their owner, arrows fly on their own. Dead ones are removed.
void update_attacks(Game& game, float dt, Vector2 player_pos, Vector2 ai_pos) {
    attack_list& pa = game.player_attack_list();
    attack_list& aa = game.ai_attack_list();
    for (auto& a : pa) if (a->is_Alive()) a->update(dt, player_pos);   // follows PLAYER
    for (auto& a : aa) if (a->is_Alive()) a->update(dt, ai_pos);       // follows AI

    auto is_dead = [](const std::unique_ptr<base_attack>& a) { return !a->is_Alive(); };
    pa.erase(std::remove_if(pa.begin(), pa.end(), is_dead), pa.end());
    aa.erase(std::remove_if(aa.begin(), aa.end(), is_dead), aa.end());
}

void draw_attacks(Game& game) {
    for (auto& a : game.player_attack_list()) if (a->is_Alive()) a->draw(a->get_pos());
    for (auto& a : game.ai_attack_list())     if (a->is_Alive()) a->draw(a->get_pos());
}

// Both directions of combat damage in one place. Arrows are consumed on hit; a melee swing only hits once.
void resolve_damage(Game& game, Player& player, ai_combat_system& ai) {
    attack_list& pa = game.player_attack_list();
    attack_list& aa = game.ai_attack_list();
    auto& hit_once = game.hit_once();

    for (auto& up : pa) {                                           // player -> AI
        base_attack* a = up.get();
        if (!a->is_Alive() || hit_once.count(a)) continue;
        if (!a->get_hitbox().check_collision(ai.get_hitbox())) continue;
        ai.take_damage((int)a->get_damage(), player.get_pos());                       // the AI learns from this automatically
        if (dynamic_cast<ranged_attack*>(a)) a->kill(); else hit_once.insert(a);
    }
    for (auto& up : aa) {                                           // AI -> player
        base_attack* a = up.get();
        if (!a->is_Alive() || hit_once.count(a)) continue;
        if (!a->get_hitbox().check_collision(player.get_hitbox())) continue;
        const int dmg = (int)a->get_damage();
        const bool melee = dynamic_cast<meele_attack*>(a) != nullptr;
        player.take_damage(dmg);
        ai.on_hit_player(dmg, melee);
        if (melee) hit_once.insert(a); else a->kill();
    }
    // forget swings that no longer exist (heap addresses get reused)
    for (auto it = hit_once.begin(); it != hit_once.end();) {
        bool alive = false;
        for (auto& up : pa) if (up.get() == *it) { alive = true; break; }
        if (!alive) for (auto& up : aa) if (up.get() == *it) { alive = true; break; }
        it = alive ? std::next(it) : hit_once.erase(it);
    }
}

} 


void render_start_menu()
{
    const int screenW = GetScreenWidth();
    const int screenH = GetScreenHeight();
    float t = (float)GetTime();

    // ---------- Neon Marble Background ----------
    ClearBackground(Color{8, 5, 18, 255});          // very dark purple-black

    // Layered moving neon ribbons
    for (int i = 0; i < 18; i++)
    {
        float speed  = 0.25f + i * 0.07f;
        float offset = i * 47.0f;
        float y      = screenH * 0.5f + sinf(t * speed + offset) * (180.0f + i * 12.0f);
        float thickness = 90.0f + i * 8.0f;

        Color c = ColorFromHSV(fmodf(t * 18.0f + i * 28.0f, 360.0f), 0.95f, 1.0f);
        c.a = 35;                                   // soft glow

        DrawRectangle(0, (int)(y - thickness * 0.5f), screenW, (int)thickness, c);
    }

    // Extra floating neon orbs
    for (int i = 0; i < 9; i++)
    {
        float x = screenW * 0.5f + cosf(t * 0.4f + i * 1.7f) * (400.0f + i * 40.0f);
        float y = screenH * 0.5f + sinf(t * 0.55f + i * 1.3f) * (220.0f + i * 30.0f);
        float r = 60.0f + i * 12.0f;

        Color c = ColorFromHSV(fmodf(t * 25.0f + i * 40.0f, 360.0f), 1.0f, 1.0f);
        c.a = 40;
        DrawCircle((int)x, (int)y, r, c);
    }

    // Soft vignette (makes center pop)
    DrawRectangle(0, 0, screenW, 80, Color{0, 0, 0, 120});
    DrawRectangle(0, screenH - 80, screenW, 80, Color{0, 0, 0, 120});

    // ---------- Title ----------
    const char* title = "super AI combat";
    int titleSize = 72;
    int titleW = MeasureText(title, titleSize);
    DrawText(title, screenW/2 - titleW/2, 160, titleSize, Color{0, 255, 255, 255});

    // Soft glow under title
    DrawText(title, screenW/2 - titleW/2 - 2, 162, titleSize, Color{255, 0, 255, 60});

    // ---------- Buttons ----------
    const int btnW = 320;
    const int btnH = 64;
    const int btnX = screenW/2 - btnW/2;

    // Button positions
    Rectangle playBtn    = { (float)btnX, 380.0f, (float)btnW, (float)btnH };
    Rectangle creditsBtn = { (float)btnX, 470.0f, (float)btnW, (float)btnH };
    Rectangle quitBtn    = { (float)btnX, 560.0f, (float)btnW, (float)btnH };

    Vector2 mouse = GetMousePosition();

    // Helper to draw a neon button
    auto drawNeonButton = [&](Rectangle r, const char* text, bool hovered)
    {
        Color base  = hovered ? Color{0, 255, 200, 255} : Color{0, 180, 255, 200};
        Color glow  = hovered ? Color{0, 255, 200, 80}  : Color{0, 180, 255, 40};
        Color textC = hovered ? WHITE : Color{200, 255, 255, 255};

        // Outer glow
        DrawRectangleRounded(
            Rectangle{r.x - 6, r.y - 6, r.width + 12, r.height + 12},
            0.3f, 8, glow);

        // Main button
        DrawRectangleRounded(r, 0.25f, 8, base);

        // Inner dark
        DrawRectangleRounded(
            Rectangle{r.x + 4, r.y + 4, r.width - 8, r.height - 8},
            0.2f, 6, Color{10, 15, 30, 220});

        // Text
        int tw = MeasureText(text, 28);
        DrawText(text,
                 (int)(r.x + r.width/2 - tw/2),
                 (int)(r.y + r.height/2 - 14),
                 28, textC);
    };

    bool playHover    = CheckCollisionPointRec(mouse, playBtn);
    bool creditsHover = CheckCollisionPointRec(mouse, creditsBtn);
    bool quitHover    = CheckCollisionPointRec(mouse, quitBtn);

    drawNeonButton(playBtn,    "PLAY",     playHover);
    drawNeonButton(creditsBtn, "CREDITS",  creditsHover);
    drawNeonButton(quitBtn,    "QUIT",     quitHover);

    // Footer
    DrawText("v0.2  |  Training Build", 20, screenH - 40, 18, Color{150, 150, 180, 180});
}


void update_start_menu(Game &game)
{
    const int screenW = GetScreenWidth();
    const int btnW = 320;
    const int btnH = 64;
    const int btnX = screenW/2 - btnW/2;

    Rectangle playBtn    = { (float)btnX, 380.0f, (float)btnW, (float)btnH };
    Rectangle creditsBtn = { (float)btnX, 470.0f, (float)btnW, (float)btnH };
    Rectangle quitBtn    = { (float)btnX, 560.0f, (float)btnW, (float)btnH };

    Vector2 mouse = GetMousePosition();
    bool clicked = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);



    if (clicked && CheckCollisionPointRec(mouse, playBtn))
    {
        game.change_ui_state(game_ui_state::SETUP_MENU);
    }

    if (clicked && CheckCollisionPointRec(mouse, creditsBtn))
    {

    }

    if (clicked && CheckCollisionPointRec(mouse, quitBtn))
    {
        in_game = false;
    }
}



void render_setup_menu(std::string& name, Color& color, Shape& shape, int& step) {
    const int sw = GetScreenWidth();
    const int sh = GetScreenHeight();
    float t = (float)GetTime();

    ClearBackground(Color{6, 4, 14, 255});

    for (int i = 0; i < 12; i++) {
        float y = sh * 0.5f + sinf(t * 0.3f + i * 1.1f) * (140 + i * 18);
        Color c = ColorFromHSV(fmodf(t * 15 + i * 40, 360), 0.9f, 1.0f);
        c.a = 28;
        DrawRectangle(0, (int)(y - 50), sw, 100, c);
    }

    const char* title = "CREATE YOUR FIGHTER";
    int tw = MeasureText(title, 42);
    DrawText(title, sw/2 - tw/2, 90, 42, Color{0, 255, 255, 255});

    // ---------- NAME ----------
    if (step == 0)
    {
        DrawText("Enter your name:", sw/2 - 110, 220, 28, LIGHTGRAY);

        Rectangle box = {(float)(sw/2 - 180), 270, 360, 50};
        DrawRectangleRec(box, Color{20, 20, 40, 220});
        DrawRectangleLinesEx(box, 2, Color{0, 200, 255, 255});
        DrawText(name.c_str(), (int)box.x + 12, (int)box.y + 12, 28, WHITE);

        if (((int)(t * 2)) % 2 == 0)
            DrawText("_", (int)box.x + 12 + MeasureText(name.c_str(), 28), (int)box.y + 12, 28, Color{0, 255, 200, 255});

        Rectangle nextBtn = {(float)(sw/2 - 100), 360, 200, 50};
        bool hover = CheckCollisionPointRec(GetMousePosition(), nextBtn);
        DrawRectangleRounded(nextBtn, 0.25f, 8, hover ? Color{0, 255, 180, 255} : Color{0, 160, 220, 200});
        DrawText("NEXT", (int)nextBtn.x + 65, (int)nextBtn.y + 12, 28, BLACK);
    }

    // ---------- COLOR ----------
    else if (step == 1)
    {
        DrawText("Pick a color:", sw/2 - 90, 200, 28, LIGHTGRAY);

        Vector2 center = {(float)sw/2, 420};
        float radius = 140;

        for (int i = 0; i < 360; i += 3) {
            Color c = ColorFromHSV((float)i, 1.0f, 1.0f);
            DrawCircleSector(center, radius, (float)i, (float)i + 4, 8, c);
        }

        DrawCircle((int)center.x, (int)center.y, 38, color);
        DrawCircleLines((int)center.x, (int)center.y, 40, WHITE);

        Rectangle nextBtn = {(float)(sw/2 - 100), 620, 200, 50};
        bool hover = CheckCollisionPointRec(GetMousePosition(), nextBtn);
        DrawRectangleRounded(nextBtn, 0.25f, 8, hover ? Color{0, 255, 180, 255} : Color{0, 160, 220, 200});
        DrawText("NEXT", (int)nextBtn.x + 65, (int)nextBtn.y + 12, 28, BLACK);
    }

    // ---------- SHAPE ----------
    else if (step == 2)
    {
        DrawText("Choose your shape:", sw/2 - 130, 210, 28, LIGHTGRAY);

        Rectangle rectBtn = {(float)(sw/2 - 280), 320, 160, 120};
        Rectangle circBtn = {(float)(sw/2 - 80),  320, 160, 120};
        Rectangle triBtn  = {(float)(sw/2 + 120), 320, 160, 120};

        auto drawShapeBtn = [&](Rectangle r, const char* label, bool selected) {
            Color bg = selected ? Color{0, 220, 180, 255} : Color{30, 30, 50, 220};
            DrawRectangleRounded(r, 0.2f, 8, bg);
            DrawText(label, (int)r.x + 35, (int)r.y + 45, 24, WHITE);
        };

        drawShapeBtn(rectBtn, "RECT",   shape == Shape::rect);
        drawShapeBtn(circBtn, "CIRCLE", shape == Shape::circle);
        drawShapeBtn(triBtn,  "TRI",    shape == Shape::triangle);

        Rectangle doneBtn = {(float)(sw/2 - 100), 500, 200, 50};
        bool hover = CheckCollisionPointRec(GetMousePosition(), doneBtn);
        DrawRectangleRounded(doneBtn, 0.25f, 8, hover ? Color{0, 255, 180, 255} : Color{0, 160, 220, 200});
        DrawText("DONE", (int)doneBtn.x + 65, (int)doneBtn.y + 12, 28, BLACK);
    }


    // ---------- TUTORIAL PAGES ----------
    else if (step >= 3)
    {
        // Tutorial content box
        Rectangle contentBox = {(float)(sw/2 - 340), 160, 680, 360};
        DrawRectangleRounded(contentBox, 0.15f, 12, Color{15, 12, 30, 230});
        DrawRectangleRoundedLines(contentBox, 0.15f, 12, Color{0, 200, 255, 180});

        // Page indicator
        char pageText[32];
        snprintf(pageText, sizeof(pageText), "Page %d / 3", step - 2);
        int pageW = MeasureText(pageText, 20);
        DrawText(pageText, sw/2 - pageW/2, 130, 20, Color{150, 200, 255, 200});

        // ========== EDIT THE TEXT BELOW ==========
        // You can change these strings freely.
        // Use \n for new lines.

        const char* page1 =
            "WELCOME TO SUPER AI COMBAT\n\n"
            "This is a short tutorial to get you started.\n";

        const char* page2 =
            "CONTROLS\n\n"
            "Movement:  WASD\n"
            "Melee:     Left click\n"
            "Ranged:    Right click (as long as you have arrows)";

        const char* page3 =
            "extra information\n\n"
            "- The AI learns from every fight\n"
            "- Collect arrows that spawn on the map\n"
            "- you can get upgrades after each round\n"
            "Good luck. Have fun!";

        const char* currentText = page1;
        if (step == 4) currentText = page2;
        if (step == 5) currentText = page3;

        // Draw the text (simple multi-line)
        DrawText(currentText, (int)contentBox.x + 30, (int)contentBox.y + 30, 24, Color{220, 240, 255, 255});

        // ========== END OF EDITABLE TEXT ==========

        // Buttons
        Rectangle backBtn  = {(float)(sw/2 - 260), 560, 180, 50};
        Rectangle nextBtn  = {(float)(sw/2 + 80),  560, 180, 50};

        bool backHover = CheckCollisionPointRec(GetMousePosition(), backBtn);
        bool nextHover = CheckCollisionPointRec(GetMousePosition(), nextBtn);

        // Back button (only show if not on first tutorial page)
        if (step > 3) {
            DrawRectangleRounded(backBtn, 0.25f, 8, backHover ? Color{255, 120, 80, 255} : Color{180, 80, 60, 200});
            DrawText("BACK", (int)backBtn.x + 55, (int)backBtn.y + 12, 28, WHITE);
        }

        // Next / Finish button
        const char* nextLabel = (step == 5) ? "START" : "NEXT";
        Color nextColor = nextHover ? Color{0, 255, 180, 255} : Color{0, 160, 220, 200};
        DrawRectangleRounded(nextBtn, 0.25f, 8, nextColor);
        int labelW = MeasureText(nextLabel, 28);
        DrawText(nextLabel, (int)(nextBtn.x + nextBtn.width/2 - labelW/2), (int)nextBtn.y + 12, 28, BLACK);
    }
}


void update_setup_menu(std::string& name, Color& color, Shape& shape, int& step, Game &game)
{
    Vector2 mouse = GetMousePosition();
    bool click = IsMouseButtonPressed(MOUSE_LEFT_BUTTON);

    // ---------- NAME ----------
    if (step == 0)
    {
        int key = GetCharPressed();
        while (key > 0) {
            if ((key >= 32) && (key <= 125) && name.size() < 31) {
                name += (char)key;
            }
            key = GetCharPressed();
        }

        if (IsKeyPressed(KEY_BACKSPACE) && !name.empty()) {
            name.pop_back();
        }

        Rectangle nextBtn = {(float)(GetScreenWidth()/2 - 100), 360, 200, 50};
        if (click && CheckCollisionPointRec(mouse, nextBtn) && !name.empty()) {
            step = 1;
        }
    }

    // ---------- COLOR ----------
    else if (step == 1)
    {
        Vector2 center = {(float)GetScreenWidth()/2, 420};
        float radius = 140;

        if (IsMouseButtonDown(MOUSE_LEFT_BUTTON)) {
            float dx = mouse.x - center.x;
            float dy = mouse.y - center.y;
            float dist = sqrtf(dx*dx + dy*dy);

            if (dist < radius && dist > 20) {
                float angle = atan2f(dy, dx) * RAD2DEG;
                if (angle < 0) angle += 360.0f;
                color = ColorFromHSV(angle, 1.0f, 1.0f);
            }
        }

        Rectangle nextBtn = {(float)(GetScreenWidth()/2 - 100), 620, 200, 50};
        if (click && CheckCollisionPointRec(mouse, nextBtn)) {
            step = 2;
        }
    }

    // ---------- SHAPE ----------
    else if (step == 2)
    {
        Rectangle rectBtn = {(float)(GetScreenWidth()/2 - 280), 320, 160, 120};
        Rectangle circBtn = {(float)(GetScreenWidth()/2 - 80),  320, 160, 120};
        Rectangle triBtn  = {(float)(GetScreenWidth()/2 + 120), 320, 160, 120};

        if (click && CheckCollisionPointRec(mouse, rectBtn)) shape = Shape::rect;
        if (click && CheckCollisionPointRec(mouse, circBtn)) shape = Shape::circle;
        if (click && CheckCollisionPointRec(mouse, triBtn))  shape = Shape::triangle;

        Rectangle doneBtn = {(float)(GetScreenWidth()/2 - 100), 500, 200, 50};
        if (click && CheckCollisionPointRec(mouse, doneBtn)) {
            step = 3;   // finished
        }
    }

    // ---------- TUTORIAL ----------
    else if (step >= 3)
    {
        Rectangle backBtn = {(float)(GetScreenWidth()/2 - 260), 560, 180, 50};
        Rectangle nextBtn = {(float)(GetScreenWidth()/2 + 80),  560, 180, 50};

        // Back
        if (step > 3 && click && CheckCollisionPointRec(mouse, backBtn)) {
            step--;
        }

        // Next / Start
        if (click && CheckCollisionPointRec(mouse, nextBtn)) {
            if (step < 5) {
                step++;
            }
            else {
                // finished tutorial → start the game
                step = 0; // reset for next time (optional)
                game.change_ui_state(game_ui_state::IN_GAME);
            }
        }
    }
}



void render_main_game(Game& game, Player& player, ai_combat_system& ai, arrow_handler& arrow) {
    arrow.draw_all();          // pickups (drawn here now - drawing from inside the update ran before BeginDrawing)
    player.draw();
    ai.draw();
    game.fx().draw();
    draw_attacks(game);
}

void update_main_game(float dt, Game& game, Player& player, ai_combat_system& ai, arrow_handler& arrow, bool round_on) {
    (void)round_on;
    game.fx().update(dt);

    // 1. optional scripted opponent (training)
    static bool bot_was_on = false;
    if (g_sparring_enabled && !bot_was_on) g_sparring.begin_episode(player);
    bot_was_on = g_sparring_enabled;
    if (g_sparring_enabled) {
        g_sparring.update(dt, player, ai, game.ai_attack_list(),
                          [&game](std::unique_ptr<base_attack> a) { game.spawn_player_attack(std::move(a)); });
    }

    // 2. the AI: perceive -> (re)decide -> run its state -> move -> attack
    ai.update(dt, player, game.player_attack_list(),
              [&game](std::unique_ptr<base_attack> a) { game.spawn_ai_attack(std::move(a)); });

    // 3. world
    player.update(dt);
    update_attacks(game, dt, player.get_pos(), ai.get_position());
    resolve_damage(game, player, ai);

    // 4. round over?
    if (ai.get_hp() <= 0 || !player.check_is_alive() || ai.episode_timed_out()) {
        game.end_round(player, ai, arrow);
        return;
    }

    // 5. arrows (your arrow_handler calls ai.report_arrow / add_arrow / on_arrow_pickup)
    arrow.update_all(player, ai);
}

upgrade upgrade1(upgrade_type::STANDARD, 0, "placeholder");
upgrade upgrade2(upgrade_type::STANDARD, 0, "placeholder");
upgrade upgrade3(upgrade_type::STANDARD, 0, "placeholder");

std::string titles[3]       = { "TITLE 1", "TITLE 2", "SPECIAL TITLE" };
std::string descriptions[3] = { "Description 1", "Description 2", "Golden and sparkly!" };
bool        is_special[3]   = { false, false, true };

void update_after_round(std::vector<upgrade> &list, Game &game) {
    Vector2 mouse = GetMousePosition();
    float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    float margin = sw * 0.04f, gap = sw * 0.02f;
    float w = (sw - margin * 2 - gap * 2) / 3.0f;
    float h = sh - margin * 2;

    for (int i = 0; i < 3; i++) {
        Rectangle card = { margin + i * (w + gap), margin, w, h };
        float pad = w * 0.08f, bh = h * 0.12f;
        Rectangle btn = { card.x + pad, card.y + h - bh - pad, w - pad * 2, bh };

        if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT) && CheckCollisionPointRec(mouse, btn)) {
            if (i == 0) {
                list.push_back(upgrade1);
                game.change_ui_state(game_ui_state::IN_GAME);
            }
            if (i == 1) {
                list.push_back(upgrade2);
                game.change_ui_state(game_ui_state::IN_GAME);
            }
            if (i == 2) {
                list.push_back(upgrade3);
                game.change_ui_state(game_ui_state::IN_GAME);
            }
        }
    }
}

void setup_upgrade_menu(std::vector<upgrade> &list) {
    upgrade1 = generate_upgrade_option(list);
    upgrade2 = generate_upgrade_option(list);
    upgrade3 = generate_upgrade_option(list);

    titles[0] = upgrade1.get_name();
    titles[1] = upgrade2.get_name();
    titles[2] = upgrade3.get_name();

    descriptions[0] = get_description(upgrade1.get_id());
    descriptions[1] = get_description(upgrade2.get_id());
    descriptions[2] = get_description(upgrade3.get_id());

    is_special[0] = (upgrade1.get_upgrade_type() == upgrade_type::SPECIAL);
    is_special[1] = (upgrade2.get_upgrade_type() == upgrade_type::SPECIAL);
    is_special[2] = (upgrade3.get_upgrade_type() == upgrade_type::SPECIAL);
}

void render_after_round() {

    const Color accents[3] = { {0, 220, 255, 255}, {255, 80, 200, 255}, {110, 255, 120, 255} };
    const Color dark = { 15, 15, 25, 255 };

    float t = (float)GetTime();
    Vector2 mouse = GetMousePosition();
    float sw = (float)GetScreenWidth(), sh = (float)GetScreenHeight();
    float margin = sw * 0.04f, gap = sw * 0.02f;
    float w = (sw - margin * 2 - gap * 2) / 3.0f;
    float h = sh - margin * 2;


    {
        auto rnd = [](int k, float salt) {
            float v = sinf(k * 12.9898f + salt * 78.233f) * 43758.5453f;
            return v - floorf(v);
        };

        // 1) four-corner gradient, each corner a different hue, all cycling through the spectrum
        float hs = t * 30.0f;
        Color c_tl = ColorFromHSV(fmodf(hs,          360.0f), 0.80f, 0.60f);
        Color c_bl = ColorFromHSV(fmodf(hs +  90.0f, 360.0f), 0.80f, 0.45f);
        Color c_br = ColorFromHSV(fmodf(hs + 180.0f, 360.0f), 0.80f, 0.60f);
        Color c_tr = ColorFromHSV(fmodf(hs + 270.0f, 360.0f), 0.80f, 0.45f);
        DrawRectangleGradientEx({ 0, 0, sw, sh }, c_tl, c_bl, c_br, c_tr);

        // 2) big soft color blobs drifting around (lava-lamp feel)
        for (int k = 0; k < 6; k++) {
            float bx = sw * (0.5f + 0.45f * sinf(t * (0.25f + rnd(k, 1.0f) * 0.3f) + k * 2.0f));
            float by = sh * (0.5f + 0.45f * cosf(t * (0.20f + rnd(k, 2.0f) * 0.3f) + k * 1.3f));
            float br = sh * (0.25f + 0.2f * rnd(k, 3.0f));
            Color bc = ColorFromHSV(fmodf(hs * 1.5f + k * 60.0f, 360.0f), 0.7f, 1.0f);
            for (int s = 0; s < 5; s++)   // stacked circles = soft edge
                DrawCircleV({ bx, by }, br * (1.0f - s * 0.18f), Fade(bc, 0.07f));
        }

        // 3) fast soda bubbles in every color
        for (int k = 0; k < 90; k++) {
            float r      = 6.0f + rnd(k, 1.0f) * 40.0f;
            float speed  = 140.0f + rnd(k, 2.0f) * 320.0f;   // faster (was 25-115)
            float total  = sh + r * 2.0f;
            float y      = sh + r - fmodf(t * speed + rnd(k, 3.0f) * total, total);
            float x      = rnd(k, 4.0f) * sw + sinf(t * (2.0f + rnd(k, 5.0f) * 2.0f) + k) * 26.0f;
            Color bc     = ColorFromHSV(fmodf(rnd(k, 6.0f) * 360.0f + t * 60.0f, 360.0f), 0.65f, 1.0f);

            DrawCircleV({ x, y }, r, Fade(bc, 0.25f));
            DrawCircleLinesV({ x, y }, r, Fade(bc, 0.9f));
            DrawCircleV({ x - r * 0.35f, y - r * 0.35f }, r * 0.18f, Fade(WHITE, 0.75f));
        }

        // light dim so cards pop (lower or delete for a brighter look)
        DrawRectangle(0, 0, (int)sw, (int)sh, Fade(BLACK, 0.15f));
    }

    // ===== CARDS =====
    for (int i = 0; i < 3; i++) {
        Rectangle card = { margin + i * (w + gap), margin, w, h };
        bool hover = CheckCollisionPointRec(mouse, card);
        if (hover) card.y -= 12;

        bool sp = is_special[i];
        float pulse = 0.5f + 0.5f * sinf(t * 4.0f + i);
        Color accent = sp ? Color{255, 200, 40, 255} : accents[i];
        Color body   = sp ? Color{70, 45, 5, 255}    : Color{22, 22, 45, 255};

        // shadow + glow
        DrawRectangleRounded({ card.x + 6, card.y + 10, w, h }, 0.06f, 12, Fade(BLACK, 0.5f));
        float glow = sp ? (0.5f + 0.5f * pulse) : (hover ? 0.8f : 0.0f);
        for (int k = 1; k <= 4; k++) {
            Rectangle g = { card.x - k * 4.0f, card.y - k * 4.0f, w + k * 8.0f, h + k * 8.0f };
            DrawRectangleRoundedLinesEx(g, 0.06f, 12, 3.0f, Fade(accent, glow * 0.25f / k));
        }

        // body + border
        DrawRectangleRounded(card, 0.06f, 12, body);
        DrawRectangleRoundedLinesEx(card, 0.06f, 12, 4.0f, sp ? Fade(accent, 0.7f + 0.3f * pulse) : accent);

        float pad = w * 0.06f;

        // header label
        Rectangle head = { card.x + pad, card.y + pad, w - pad * 2, h * 0.13f };
        DrawRectangleRounded(head, 0.3f, 10, accent);
        int ts = (int)(w * 0.09f);
        while (ts > 10 && MeasureText(titles[i].c_str(), ts) > head.width - 16) ts--;
        DrawText(titles[i].c_str(),
                 (int)(head.x + head.width / 2 - MeasureText(titles[i].c_str(), ts) / 2.0f),
                 (int)(head.y + (head.height - ts) / 2), ts, dark);

        // icon placeholder
        float ir = w * 0.16f;
        Vector2 ic = { card.x + w / 2, head.y + head.height + h * 0.05f + ir };
        DrawCircleV(ic, ir, Fade(accent, 0.25f));
        DrawCircleLinesV(ic, ir, accent);
        // TODO: draw the upgrade icon here (replace the "?")
        DrawText("?", (int)(ic.x - MeasureText("?", (int)ir) / 2.0f), (int)(ic.y - ir * 0.5f), (int)ir, accent);

        // description (auto word-wrap)
        float pad2 = w * 0.08f, bh = h * 0.12f;
        Rectangle btn = { card.x + pad2, card.y + h - bh - pad2, w - pad2 * 2, bh };
        float dy = ic.y + ir + h * 0.04f;
        int ds = (int)(w * 0.06f);
        std::string line, word;
        const std::string& txt = descriptions[i];
        for (size_t c = 0; c <= txt.size(); c++) {
            if (c == txt.size() || txt[c] == ' ') {
                std::string test = line.empty() ? word : line + " " + word;
                if (!line.empty() && MeasureText(test.c_str(), ds) > w - pad * 2) {
                    DrawText(line.c_str(), (int)(card.x + w / 2 - MeasureText(line.c_str(), ds) / 2.0f), (int)dy, ds, RAYWHITE);
                    dy += ds * 1.35f;
                    line = word;
                } else line = test;
                word.clear();
            } else word += txt[c];
        }
        if (!line.empty())
            DrawText(line.c_str(), (int)(card.x + w / 2 - MeasureText(line.c_str(), ds) / 2.0f), (int)dy, ds, RAYWHITE);

        // CHOOSE button
        bool over = CheckCollisionPointRec(mouse, btn);
        Rectangle b = btn;
        if (over && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) b.y += 3;
        DrawRectangleRounded({ b.x, b.y + 4, b.width, b.height }, 0.35f, 10, Fade(BLACK, 0.45f));
        DrawRectangleRounded(b, 0.35f, 10, over ? WHITE : accent);
        int bs = (int)(b.height * 0.5f);
        DrawText("CHOOSE", (int)(b.x + b.width / 2 - MeasureText("CHOOSE", bs) / 2.0f), (int)(b.y + (b.height - bs) / 2), bs, dark);

        // sparkles (stateless, driven by time)
        if (sp) {
            for (int k = 0; k < 40; k++) {
                float seed  = k * 0.618034f;
                float phase = fmodf(t * (0.25f + 0.35f * fmodf(seed * 7.0f, 1.0f)) + seed, 1.0f);
                float x = card.x + fmodf(seed * 13.37f, 1.0f) * w;
                float y = card.y + h * (1.0f - phase);
                float a = sinf(phase * PI);
                float sz = (4.0f + 7.0f * fmodf(seed * 5.0f, 1.0f)) * (0.5f + 0.5f * a);
                DrawLineEx({ x - sz, y }, { x + sz, y }, 2.0f, Fade(GOLD, a));
                DrawLineEx({ x, y - sz }, { x, y + sz }, 2.0f, Fade(GOLD, a));
                DrawCircleV({ x, y }, sz * 0.25f, Fade(WHITE, a));
            }
        }
    }
}

void render_end_menu() {
}