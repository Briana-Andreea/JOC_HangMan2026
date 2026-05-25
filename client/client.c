#include "raylib.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// ─── Constante ────────────────────────────────────────────────────────────────
#define SCREEN_W      1280
#define SCREEN_H      720
#define PORT          8080
#define MAX_WORD_LEN  64
#define BUFFER_SIZE   512
#define MAX_WORDS     300

// ─── Culori tema ──────────────────────────────────────────────────────────────
#define BG_COLOR        (Color){15, 15, 35, 255}
#define PANEL_COLOR     (Color){25, 25, 55, 255}
#define ACCENT1         (Color){100, 220, 255, 255}
#define ACCENT2         (Color){255, 100, 180, 255}
#define ACCENT3         (Color){100, 255, 180, 255}
#define BTN_NORMAL      (Color){40, 40, 80, 255}
#define BTN_HOVER       (Color){60, 60, 120, 255}
#define BTN_BORDER      (Color){100, 100, 200, 255}
#define TEXT_MAIN       (Color){220, 220, 255, 255}
#define TEXT_DIM        (Color){120, 120, 160, 255}
#define TEXT_GOOD       (Color){100, 255, 150, 255}
#define TEXT_BAD        (Color){255, 100, 100, 255}
#define TEXT_WARN       (Color){255, 200, 80, 255}
#define EASY_COLOR      (Color){80, 200, 120, 255}
#define MEDIUM_COLOR    (Color){255, 180, 50, 255}
#define HARD_COLOR      (Color){255, 80, 80, 255}

// ─── Enum-uri ─────────────────────────────────────────────────────────────────
typedef enum {
    SCREEN_MENU = 0,
    SCREEN_DIFFICULTY,
    SCREEN_SINGLEPLAYER,
    SCREEN_MULTIPLAYER,
    SCREEN_GAMEOVER
} GameScreen;

typedef enum {
    DIFF_EASY = 0,
    DIFF_MEDIUM,
    DIFF_HARD
} Difficulty;

// ─── Stare Single Player ──────────────────────────────────────────────────────
typedef struct {
    char   secret[MAX_WORD_LEN];
    int    guessed[26];
    char   wrong[27];
    int    wrong_count;
    int    max_lives;
    int    game_over;     // 0=activ 1=win 2=lose
    int    hint_count;    // cate indicii au mai ramas
    int    hint_cost;     // vieti pe indiciu
    char   hint_text[128];
    int    hint_visible;
    float  time_left;     // pt hard
    int    timed;         // 1 daca e hard
    Difficulty diff;
} SPGame;

// ─── Stare Multiplayer ────────────────────────────────────────────────────────
typedef struct {
    char display[MAX_WORD_LEN * 2];
    char my_wrong[27];       // greselile mele individuale
    int  game_over;
    int  player_count;
    char message[256];
    int  current_turn;
    int  my_idx;
    int  waiting;
    int  winner;
    char secret[MAX_WORD_LEN];
    int  lives_all[3];
    int  wcount_all[3];  // wrong count per jucator (pt spanzuratoare)
    int  elim_all[3];
    int  hints_used;     // indicii folosite de mine
} MPState;

int      mp_sock = -1;
MPState  mp_state;
pthread_mutex_t mp_mutex = PTHREAD_MUTEX_INITIALIZER;

// ─── Cuvinte ──────────────────────────────────────────────────────────────────
char words[MAX_WORDS][MAX_WORD_LEN];
int  word_count = 0;

void load_words(const char *path) {
    FILE *f = fopen(path, "r");
    word_count = 0;
    if (f) {
        while (fscanf(f, "%s", words[word_count]) == 1 && word_count < MAX_WORDS) {
            for (int i = 0; words[word_count][i]; i++)
                words[word_count][i] = tolower(words[word_count][i]);
            word_count++;
        }
        fclose(f);
    }
    if (word_count == 0) {
        char def[][MAX_WORD_LEN] = {
            "programare","calculator","tastatura","compilator",
            "algoritm","retea","variabila","functie","structura",
            "pointer","recursivitate","socket","protocol","memorie"
        };
        word_count = 14;
        for (int i = 0; i < word_count; i++)
            strcpy(words[i], def[i]);
    }
}

// ─── Hint automat ─────────────────────────────────────────────────────────────
void generate_hint(SPGame *g) {
    int len = strlen(g->secret);
    
    // Verificăm câte indicii au fost DEJA afișate. 
    // În funcție de asta, generăm un indiciu din ce în ce mai puternic.
    // Indicii rămase inițial: 3. La prima apăsare, hint_count devine 2.
    
    if (g->hint_count == 2) { // Primul indiciu cerut
        int vowels = 0;
        for (int i = 0; i < len; i++) {
            char c = g->secret[i];
            if (c=='a' || c=='e' || c=='i' || c=='o' || c=='u') vowels++;
        }
        const char *domains[] = {"informatica", "matematica", "stiinta", "tehnologie", "programare"};
        snprintf(g->hint_text, 128, "Indiciu 1: Cuvantul are %d litere, %d vocale si tine de %s.", 
                 len, vowels, domains[rand() % 5]);
    } 
    else if (g->hint_count == 1) { // Al doilea indiciu cerut
        snprintf(g->hint_text, 128, "Indiciu 2: Prima litera a cuvantului este '%c'.", toupper(g->secret[0]));
    } 
    else if (g->hint_count == 0) { // Al treilea indiciu cerut
        snprintf(g->hint_text, 128, "Indiciu 3: Ultima litera a cuvantului este '%c'.", toupper(g->secret[len - 1]));
    }
}

// ─── Init Single Player ───────────────────────────────────────────────────────
void sp_init(SPGame *g, Difficulty diff) {
    memset(g, 0, sizeof(SPGame));
    g->diff = diff;
    srand(time(NULL));
    strcpy(g->secret, words[rand() % word_count]);

    switch (diff) {
        case DIFF_EASY:
            g->max_lives  = 6;
            g->hint_count = 0;
            g->hint_cost  = 0;
            g->timed      = 0;
            break;
        case DIFF_MEDIUM:
            g->max_lives  = 6;
            g->hint_count = 3;
            g->hint_cost  = 2;
            g->timed      = 0;
            break;
        case DIFF_HARD:
            g->max_lives  = 8;
            g->hint_count = 3;
            g->hint_cost  = 2;
            g->timed      = 1;
            g->time_left  = 100.0f;
            break;
    }
    //generate_hint(g);
}

// ─── Guess Single Player ──────────────────────────────────────────────────────
void sp_guess(SPGame *g, char letter) {
    if (g->game_over) return;
    letter = tolower(letter);
    int idx = letter - 'a';
    if (idx < 0 || idx > 25) return;
    if (g->guessed[idx] || strchr(g->wrong, letter)) return;

    if (strchr(g->secret, letter)) {
        g->guessed[idx] = 1;
        // check win
        int win = 1;
        for (int i = 0; g->secret[i]; i++) {
            int ci = g->secret[i] - 'a';
            if (ci >= 0 && ci <= 25 && !g->guessed[ci]) { win = 0; break; }
        }
        if (win) g->game_over = 1;
    } else {
        int wlen = strlen(g->wrong);
        g->wrong[wlen]   = letter;
        g->wrong[wlen+1] = '\0';
        g->wrong_count++;
        if (g->wrong_count >= g->max_lives) g->game_over = 2;
    }
}

void sp_use_hint(SPGame *g) {
    if (g->hint_count <= 0) return;
    if (g->wrong_count + g->hint_cost > g->max_lives) return;
    
    g->wrong_count += g->hint_cost;
    g->hint_count--; // Scade numărul de indicii rămase
    
    // ACUM generăm indiciul cel nou, după ce hint_count a scăzut!
    generate_hint(g); 
    
    g->hint_visible = 1;
    if (g->wrong_count >= g->max_lives) g->game_over = 2;
}

// ─── Multiplayer recv thread ──────────────────────────────────────────────────
void parse_mp(const char *json) {
    pthread_mutex_lock(&mp_mutex);
    char *p;
    p = strstr(json, "\"display\":\"");
    if (p) { p += 11; sscanf(p, "%[^\"]", mp_state.display); }
    
    p = strstr(json, "\"my_wrong\":\"");
    if (p) { p += 12; sscanf(p, "%[^\"]", mp_state.my_wrong); }
    else mp_state.my_wrong[0] = '\0';
    
    p = strstr(json, "\"game_over\":");
    if (p) sscanf(p + 12, "%d", &mp_state.game_over);
    
    p = strstr(json, "\"players\":");
    if (p) sscanf(p + 10, "%d", &mp_state.player_count);
    
    p = strstr(json, "\"msg\":\"");
    if (p) { p += 7; sscanf(p, "%255[^\"]", mp_state.message); }
    
    p = strstr(json, "\"turn\":");
    if (p) sscanf(p + 7, "%d", &mp_state.current_turn);
    
    p = strstr(json, "\"my_idx\":");
    if (p) sscanf(p + 9, "%d", &mp_state.my_idx);
    
    p = strstr(json, "\"waiting\":");
    if (p) sscanf(p + 10, "%d", &mp_state.waiting);
    
    p = strstr(json, "\"winner\":");
    if (p) sscanf(p + 9, "%d", &mp_state.winner);
    
    p = strstr(json, "\"secret\":\"");
    if (p) { p += 10; sscanf(p, "%[^\"]", mp_state.secret); }
    
    p = strstr(json, "\"hints_used\":");
    if (p) sscanf(p + 13, "%d", &mp_state.hints_used);
    
    // lives_all
    p = strstr(json, "\"lives_all\":\"");
    if (p) { p += 13; for(int i=0;i<3;i++){mp_state.lives_all[i]=atoi(p);char*c=strchr(p,',');if(!c)break;p=c+1;} }
    // wcount_all
    p = strstr(json, "\"wcount_all\":\"");
    if (p) { p += 14; for(int i=0;i<3;i++){mp_state.wcount_all[i]=atoi(p);char*c=strchr(p,',');if(!c)break;p=c+1;} }
    // elim_all
    p = strstr(json, "\"elim_all\":\"");
    if (p) { p += 12; for(int i=0;i<3;i++){mp_state.elim_all[i]=atoi(p);char*c=strchr(p,',');if(!c)break;p=c+1;} }
    pthread_mutex_unlock(&mp_mutex);
}

void *recv_thread(void *arg) {
    char buf[BUFFER_SIZE*4], accum[BUFFER_SIZE*8]="";
    while (1) {
        int n = recv(mp_sock, buf, sizeof(buf)-1, 0);
        if (n <= 0) break;
        buf[n] = '\0';
        strncat(accum, buf, sizeof(accum)-strlen(accum)-1);
        char *nl;
        while ((nl = strchr(accum, '\n')) != NULL) {
            *nl = '\0';
            if (strlen(accum) > 2) parse_mp(accum);
            memmove(accum, nl+1, strlen(nl+1)+1);
        }
    }
    return NULL;
}

// ─── Helpers UI ──────────────────────────────────────────────────────────────
typedef struct {
    Rectangle rect;
    const char *label;
    Color color;
    Color border;
    bool hovered;
} Button;

Button make_button(float x, float y, float w, float h, const char *label, Color c, Color b) {
    return (Button){{x,y,w,h}, label, c, b, false};
}

void update_button(Button *btn) {
    Vector2 mouse = GetMousePosition();
    btn->hovered = CheckCollisionPointRec(mouse, btn->rect);
}

bool button_clicked(Button *btn) {
    return btn->hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON);
}

void draw_button(Button *btn, int font_size) {
    Color fill = btn->hovered ? BTN_HOVER : btn->color;
    Color bord = btn->hovered ? ACCENT1   : btn->border;
    DrawRectangleRounded(btn->rect, 0.3f, 8, fill);
    DrawRectangleRoundedLines(btn->rect, 0.3f, 8, bord);
    int tw = MeasureText(btn->label, font_size);
    float tx = btn->rect.x + (btn->rect.width  - tw) / 2;
    float ty = btn->rect.y + (btn->rect.height - font_size) / 2;
    Color tc = btn->hovered ? ACCENT1 : TEXT_MAIN;
    DrawText(btn->label, (int)tx, (int)ty, font_size, tc);
}

// ─── Desenare spanzuratoare ───────────────────────────────────────────────────
void draw_hangman_fancy(int wrong, int max_lives, float cx, float cy) {
    float scale = 1.2f;
    Color rope_c = (Color){160,140,100,255};
    Color body_c = ACCENT1;
    Color dead_c = TEXT_BAD;
    Color c      = (wrong >= max_lives) ? dead_c : body_c;

    // Postul
    DrawLineEx((Vector2){cx-80*scale, cy+130*scale},
               (Vector2){cx+80*scale, cy+130*scale}, 4, rope_c);
    DrawLineEx((Vector2){cx-40*scale, cy+130*scale},
               (Vector2){cx-40*scale, cy-130*scale}, 4, rope_c);
    DrawLineEx((Vector2){cx-40*scale, cy-130*scale},
               (Vector2){cx+50*scale, cy-130*scale}, 4, rope_c);
    DrawLineEx((Vector2){cx+50*scale, cy-130*scale},
               (Vector2){cx+50*scale, cy-100*scale}, 3, rope_c);

    if (wrong >= 1) // cap
        DrawCircleLines((int)(cx+50*scale), (int)(cy-75*scale), 22*scale, c);
    if (wrong >= 2) // corp
        DrawLineEx((Vector2){cx+50*scale, cy-53*scale},
                   (Vector2){cx+50*scale, cy+10*scale}, 3, c);
    if (wrong >= 3) // brat stang
        DrawLineEx((Vector2){cx+50*scale, cy-40*scale},
                   (Vector2){cx+20*scale, cy-10*scale}, 3, c);
    if (wrong >= 4) // brat drept
        DrawLineEx((Vector2){cx+50*scale, cy-40*scale},
                   (Vector2){cx+80*scale, cy-10*scale}, 3, c);
    if (wrong >= 5) // picior stang
        DrawLineEx((Vector2){cx+50*scale, cy+10*scale},
                   (Vector2){cx+25*scale, cy+50*scale}, 3, c);
    if (wrong >= 6) // picior drept
        DrawLineEx((Vector2){cx+50*scale, cy+10*scale},
                   (Vector2){cx+75*scale, cy+50*scale}, 3, c);
    // extra vieti pt hard (7,8)
    if (max_lives >= 7 && wrong >= 7)
        DrawCircle((int)(cx+50*scale), (int)(cy-75*scale), 5*scale, dead_c);
    if (max_lives >= 8 && wrong >= 8)
        DrawLineEx((Vector2){cx+30*scale, cy-80*scale},
                   (Vector2){cx+70*scale, cy-80*scale}, 2, dead_c);
}

// ─── Desenare cuvant cu underscores ──────────────────────────────────────────
void draw_word_sp(SPGame *g, float cx, float cy) {
    int len = strlen(g->secret);
    int letter_w = 44, gap = 12;
    float total = len * letter_w + (len-1) * gap;
    float start = cx - total/2;

    for (int i = 0; i < len; i++) {
        float lx = start + i * (letter_w + gap);
        char c = g->secret[i];
        int idx = c - 'a';
        // linie de baza
        DrawLineEx((Vector2){lx, cy+2}, (Vector2){lx+letter_w, cy+2}, 2, ACCENT1);
        if (idx >= 0 && idx <= 25 && g->guessed[idx]) {
            char s[2] = {c, '\0'};
            int tw = MeasureText(s, 36);
            DrawText(s, (int)(lx + (letter_w-tw)/2), (int)(cy-36), 36, TEXT_MAIN);
        }
    }
}

// ─── Desenare tastatura vizuala ────────────────────────────────────────────────
void draw_keyboard(SPGame *g, float ox, float oy) {
    const char *rows[] = {"qwertyuiop", "asdfghjkl", "zxcvbnm"};
    int key_w = 54, key_h = 54, key_gap = 8;
    for (int r = 0; r < 3; r++) {
        int len = strlen(rows[r]);
        float row_w = len * key_w + (len-1) * key_gap;
        float rx = ox - row_w/2 + r * 28;
        for (int k = 0; k < len; k++) {
            char c = rows[r][k];
            int idx = c - 'a';
            float kx = rx + k * (key_w + key_gap);
            float ky = oy + r * (key_h + key_gap);
            Rectangle kr = {kx, ky, key_w, key_h};
            Color fill, text_c;
            if (g->guessed[idx]) {
                fill   = TEXT_GOOD;
                text_c = (Color){10,10,10,255};
            } else if (strchr(g->wrong, c)) {
                fill   = (Color){60,20,20,255};
                text_c = TEXT_BAD;
            } else {
                fill   = BTN_NORMAL;
                text_c = TEXT_MAIN;
            }
            DrawRectangleRounded(kr, 0.3f, 6, fill);
            DrawRectangleRoundedLines(kr, 0.3f, 6, BTN_BORDER);
            char s[2] = {c, '\0'};
            int tw = MeasureText(s, 22);
            DrawText(s, (int)(kx+(key_w-tw)/2), (int)(ky+(key_h-22)/2), 22, text_c);
        }
    }
}

// ─── Fundal cu particule ──────────────────────────────────────────────────────
#define N_STARS 120
typedef struct { float x,y,r,speed; Color c; } Star;
Star stars[N_STARS];

void init_stars() {
    srand(42);
    for (int i = 0; i < N_STARS; i++) {
        stars[i].x = rand() % SCREEN_W;
        stars[i].y = rand() % SCREEN_H;
        stars[i].r = 0.5f + (rand()%30)/10.0f;
        stars[i].speed = 0.1f + (rand()%20)/100.0f;
        int alpha = 80 + rand()%120;
        stars[i].c = (Color){150+rand()%105, 150+rand()%105, 255, alpha};
    }
}

void draw_stars() {
    for (int i = 0; i < N_STARS; i++) {
        stars[i].y += stars[i].speed;
        if (stars[i].y > SCREEN_H) stars[i].y = 0;
        DrawCircleV((Vector2){stars[i].x, stars[i].y}, stars[i].r, stars[i].c);
    }
}

// ─── Bara de vieti ────────────────────────────────────────────────────────────
void draw_heart(float cx, float cy, float size, Color c) {
    // two circles + triangle = heart shape
    DrawCircleV((Vector2){cx - size*0.5f, cy}, size*0.5f, c);
    DrawCircleV((Vector2){cx + size*0.5f, cy}, size*0.5f, c);
    DrawTriangle(
        (Vector2){cx - size, cy},
        (Vector2){cx + size, cy},
        (Vector2){cx, cy + size*1.4f},
        c
    );
}

void draw_lives_bar(int wrong, int max_lives, float x, float y) {
    int lives_left = max_lives - wrong;
    DrawText("Vieti:", (int)x, (int)y, 22, TEXT_DIM);
    for (int i = 0; i < max_lives; i++) {
        Color c = (i < lives_left) ? TEXT_BAD : (Color){50,50,80,255};
        draw_heart(x + 90 + i*34, y + 10, 9, c);
    }
}

// ─── Timer bar ────────────────────────────────────────────────────────────────
void draw_timer(float time_left, float x, float y, float w) {
    float ratio = time_left / 100.0f;
    Color c = ratio > 0.5f ? TEXT_GOOD : (ratio > 0.25f ? TEXT_WARN : TEXT_BAD);
    DrawRectangleRounded((Rectangle){x,y,w,18}, 0.5f, 6, (Color){30,30,60,255});
    DrawRectangleRounded((Rectangle){x,y,w*ratio,18}, 0.5f, 6, c);
    char tbuf[32];
    snprintf(tbuf, 32, "%.0fs", time_left);
    int tw = MeasureText(tbuf, 22);
    DrawText(tbuf, (int)(x+w/2-tw/2), (int)(y+22), 22, c);
}

// ─── Screen: MENU ─────────────────────────────────────────────────────────────
void draw_menu(Button *btn_sp, Button *btn_mp) {
    // titlu
    int tw = MeasureText("HANGMAN", 80);
    DrawText("HANGMAN", SCREEN_W/2 - tw/2, 100, 80, ACCENT1);
    tw = MeasureText("Multiplayer Edition", 30);
    DrawText("Multiplayer Edition", SCREEN_W/2 - tw/2, 200, 30, ACCENT2);

    // decorativ - linie
    DrawLineEx((Vector2){SCREEN_W/2-300, 250}, (Vector2){SCREEN_W/2+300, 250}, 1, BTN_BORDER);

    draw_button(btn_sp, 32);
    draw_button(btn_mp, 32);

    tw = MeasureText("Apasa ESC pentru a iesi", 18);
    DrawText("Apasa ESC pentru a iesi", SCREEN_W/2-tw/2, SCREEN_H-40, 18, TEXT_DIM);
}

// ─── Screen: DIFFICULTY ───────────────────────────────────────────────────────
void draw_difficulty(Button *easy, Button *med, Button *hard, Button *back) {
    int tw = MeasureText("Alege dificultatea", 50);
    DrawText("Alege dificultatea", SCREEN_W/2-tw/2, 80, 50, TEXT_MAIN);

    // Easy card
    DrawRectangleRounded((Rectangle){SCREEN_W/2-470, 160, 280, 360}, 0.1f, 8, PANEL_COLOR);
    DrawRectangleRoundedLines((Rectangle){SCREEN_W/2-470, 160, 280, 360}, 0.1f, 8, EASY_COLOR);
    tw = MeasureText("EASY", 34);
    DrawText("EASY", SCREEN_W/2-470+140-tw/2, 190, 34, EASY_COLOR);
    DrawText("6 vieti", SCREEN_W/2-470+20, 250, 20, TEXT_MAIN);
    DrawText("Tastatura vizuala", SCREEN_W/2-470+20, 280, 18, TEXT_DIM);
    DrawText("Litere incercate", SCREEN_W/2-470+20, 305, 18, TEXT_DIM);
    DrawText("vizibile mereu", SCREEN_W/2-470+20, 328, 18, TEXT_DIM);
    draw_button(easy, 22);

    // Medium card
    DrawRectangleRounded((Rectangle){SCREEN_W/2-130, 160, 280, 360}, 0.1f, 8, PANEL_COLOR);
    DrawRectangleRoundedLines((Rectangle){SCREEN_W/2-130, 160, 280, 360}, 0.1f, 8, MEDIUM_COLOR);
    tw = MeasureText("MEDIUM", 34);
    DrawText("MEDIUM", SCREEN_W/2-130+140-tw/2, 190, 34, MEDIUM_COLOR);
    DrawText("6 vieti", SCREEN_W/2-130+20, 250, 20, TEXT_MAIN);
    DrawText("Fara tastatura", SCREEN_W/2-130+20, 280, 18, TEXT_DIM);
    DrawText("3x indiciu '?'", SCREEN_W/2-130+20, 305, 18, TEXT_DIM);
    DrawText("(costa 2 vieti)", SCREEN_W/2-130+20, 328, 18, TEXT_DIM);
    draw_button(med, 22);

    // Hard card
    DrawRectangleRounded((Rectangle){SCREEN_W/2+210, 160, 280, 360}, 0.1f, 8, PANEL_COLOR);
    DrawRectangleRoundedLines((Rectangle){SCREEN_W/2+210, 160, 280, 360}, 0.1f, 8, HARD_COLOR);
    tw = MeasureText("HARD", 34);
    DrawText("HARD", SCREEN_W/2+210+140-tw/2, 190, 34, HARD_COLOR);
    DrawText("8 vieti", SCREEN_W/2+210+20, 250, 20, TEXT_MAIN);
    DrawText("Contra timp: 100s", SCREEN_W/2+210+20, 280, 18, TEXT_DIM);
    DrawText("3x indiciu '?'", SCREEN_W/2+210+20, 305, 18, TEXT_DIM);
    DrawText("(costa 2 vieti)", SCREEN_W/2+210+20, 328, 18, TEXT_DIM);
    draw_button(hard, 22);

    draw_button(back, 22);
}

// ─── Screen: SINGLEPLAYER ─────────────────────────────────────────────────────
void draw_singleplayer(SPGame *g, Button *hint_btn, Button *menu_btn) {
    // Diff label
    const char *dlabel[] = {"EASY","MEDIUM","HARD"};
    Color dcol[] = {EASY_COLOR, MEDIUM_COLOR, HARD_COLOR};
    int tw = MeasureText(dlabel[g->diff], 28);
    DrawText(dlabel[g->diff], SCREEN_W/2-tw/2, 30, 28, dcol[g->diff]);

    // Panel stanga - spanzuratoare + vieti
    DrawRectangleRounded((Rectangle){20,60,400,620}, 0.05f, 8, PANEL_COLOR);
    draw_hangman_fancy(g->wrong_count, g->max_lives, 220, 310);
    draw_lives_bar(g->wrong_count, g->max_lives, 30, 620);

    // Timer pt hard
    if (g->timed)
        draw_timer(g->time_left, 30, 655, 360);

    // Panel dreapta - cuvant + input
    DrawRectangleRounded((Rectangle){440,60,820,620}, 0.05f, 8, PANEL_COLOR);

    // Cuvant
    draw_word_sp(g, 850, 250);

    // Litere gresite (easy: mereu; altfel: in wrong)
    if (g->diff == DIFF_EASY || strlen(g->wrong) > 0) {
        DrawText("Litere gresite:", 460, 320, 22, TEXT_DIM);
        char wdisp[54] = "";
        for (int i = 0; g->wrong[i]; i++) {
            char s[3] = {g->wrong[i], ' ', '\0'};
            strncat(wdisp, s, 53);
        }
        DrawText(wdisp, 460, 348, 26, TEXT_BAD);
    }

    // Tastatura (easy)
    if (g->diff == DIFF_EASY)
        draw_keyboard(g, 850, 390);

    // Buton hint (medium, hard)
    if (g->diff != DIFF_EASY) {
        char hlabel[32];
        snprintf(hlabel, 32, "? Indiciu (%d ramase)", g->hint_count);
        // update label dinamic
        hint_btn->label = (g->hint_count > 0) ? "? Indiciu" : "Fara indicii";
        Color hc = (g->hint_count > 0) ? BTN_NORMAL : (Color){40,30,30,255};
        hint_btn->color  = hc;
        hint_btn->border = (g->hint_count > 0) ? ACCENT2 : TEXT_DIM;
        draw_button(hint_btn, 22);

        char cost_s[64];
        snprintf(cost_s, 64, "Indicii: %d  |  Cost: %d vieti", g->hint_count, g->hint_cost);
        DrawText(cost_s, 460, 645, 18, TEXT_DIM);
    }

    // Hint text
    if (g->hint_visible) {
        DrawRectangleRounded((Rectangle){40,670,760,50}, 0.3f, 8, (Color){30,30,70,255});
        DrawRectangleRoundedLines((Rectangle){40, 670, 760, 50}, 0.3f, 8, ACCENT2);
        DrawText(g->hint_text, 55, 683, 18, ACCENT2);
    }

    draw_button(menu_btn, 20);

    // Game over overlay
    if (g->game_over) {
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){0,0,0,160});
        if (g->game_over == 1) {
            tw = MeasureText("AI CASTIGAT!", 100);
            DrawText("AI CASTIGAT!", SCREEN_W/2-tw/2, SCREEN_H/2-80, 100, TEXT_GOOD);
        } else {
            tw = MeasureText("AI PIERDUT!", 100);
            DrawText("AI PIERDUT!", SCREEN_W/2-tw/2, SCREEN_H/2-120, 100, TEXT_BAD);
            char sw[128];
            snprintf(sw, 128, "Cuvantul era: %s", g->secret);
            tw = MeasureText(sw, 40);
            DrawText(sw, SCREEN_W/2-tw/2, SCREEN_H/2+20, 40, TEXT_WARN);
        }
        DrawText("Apasa R pentru restart  |  ESC pentru meniu", 
                 SCREEN_W/2 - MeasureText("Apasa R pentru restart  |  ESC pentru meniu", 28)/2,
                 SCREEN_H/2+140, 28, TEXT_DIM);
    }
}

// ─── Screen: MULTIPLAYER ──────────────────────────────────────────────────────
void draw_multiplayer(MPState *s, char *ip_buf, int ip_editing, Button *conn_btn, Button *menu_btn) {
    Color player_colors[3] = {ACCENT1, ACCENT2, ACCENT3};

    if (mp_sock < 0) {
        // ── Ecran conectare ──────────────────────────────────────────────────
        int tw = MeasureText("MULTIPLAYER", 60);
        DrawText("MULTIPLAYER", SCREEN_W/2-tw/2, 80, 60, ACCENT1);
        tw = MeasureText("Conecteaza-te la server", 26);
        DrawText("Conecteaza-te la server", SCREEN_W/2-tw/2, 160, 26, TEXT_DIM);
        Rectangle ip_box = {SCREEN_W/2-300, 220, 600, 70};
        DrawRectangleRounded(ip_box, 0.2f, 8, ip_editing ? BTN_HOVER : PANEL_COLOR);
        DrawRectangleRoundedLines(ip_box, 0.2f, 8, ip_editing ? ACCENT1 : BTN_BORDER);
        DrawText("IP:", SCREEN_W/2-280, 242, 22, TEXT_DIM);
        DrawText(ip_buf, SCREEN_W/2-240, 242, 26, TEXT_MAIN);
        if (ip_editing && ((int)(GetTime()*2) % 2 == 0))
            DrawText("|", SCREEN_W/2-240+MeasureText(ip_buf,26), 242, 26, ACCENT1);
        tw = MeasureText("Click pe casuta pentru a edita IP-ul", 20);
        DrawText("Click pe casuta pentru a edita IP-ul", SCREEN_W/2-tw/2, 305, 20, TEXT_DIM);
        draw_button(conn_btn, 26);
        draw_button(menu_btn, 20);
        return;
    }

    // ── Header: cine sunt eu ─────────────────────────────────────────────────
    char my_label[48];
    snprintf(my_label, 48, "Tu esti: Jucator %d", s->my_idx+1);
    int tw = MeasureText(my_label, 26);
    DrawText(my_label, SCREEN_W/2-tw/2, 6, 26, player_colors[s->my_idx % 3]);

    if (s->waiting) {
        // ── Asteptare ────────────────────────────────────────────────────────
        tw = MeasureText("Asteptam jucatori...", 40);
        DrawText("Asteptam jucatori...", SCREEN_W/2-tw/2, 180, 40, TEXT_WARN);
        char pc[64];
        snprintf(pc, 64, "Conectati: %d  (minim 2 pentru start)", s->player_count);
        tw = MeasureText(pc, 22);
        DrawText(pc, SCREEN_W/2-tw/2, 240, 22, TEXT_DIM);
        for (int i = 0; i < s->player_count && i < 3; i++) {
            char pl[48];
            snprintf(pl, 48, "Jucator %d%s", i+1, (i==s->my_idx)?" (tu)":"");
            tw = MeasureText(pl, 26);
            DrawText(pl, SCREEN_W/2-tw/2, 300+i*44, 26, player_colors[i]);
        }
        draw_button(menu_btn, 20);
        return;
    }

    // ── Joc activ ────────────────────────────────────────────────────────────
    int np = s->player_count < 1 ? 1 : (s->player_count > 3 ? 3 : s->player_count);
    int panel_w = (SCREEN_W - 20 - 10*(np-1)) / np;

    for (int pi = 0; pi < np; pi++) {
        int px = 10 + pi*(panel_w+10);
        int py = 40;
        int ph = 390;
        Color pc2 = player_colors[pi];
        int is_me   = (pi == s->my_idx);
        int is_turn = (pi == s->current_turn && !s->elim_all[pi]);

        // Panou background
        Color bg = is_turn ? (Color){25,35,65,255} : PANEL_COLOR;
        DrawRectangleRounded((Rectangle){px,py,panel_w,ph}, 0.05f, 8, bg);
        Color border_c = is_turn ? pc2 : (is_me ? (Color){80,80,140,255} : BTN_BORDER);
        DrawRectangleRoundedLines((Rectangle){px,py,panel_w,ph}, 0.05f, 8, border_c);

        // Label jucator
        char plabel[48];
        snprintf(plabel, 48, "Jucator %d%s", pi+1, is_me?" (tu)":"");
        int ltw = MeasureText(plabel, 20);
        DrawText(plabel, px+panel_w/2-ltw/2, py+6, 20, pc2);

        // Randul
        if (is_turn && !s->elim_all[pi]) {
            const char *rt = is_me ? ">>> RANDUL TAU <<<" : ">>> JOACA <<<";
            int rtw = MeasureText(rt, 16);
            DrawText(rt, px+panel_w/2-rtw/2, py+30, 16,
                     is_me ? TEXT_GOOD : TEXT_WARN);
        }

        // Spanzuratoare bazata pe wrong_count individual
        int wc = s->wcount_all[pi];
        if (wc > 6) wc = 6;
        draw_hangman_fancy(wc, 6, px+panel_w/2, py+210);

        // Inimi individuale
        float hstart = px + panel_w/2 - (6*20)/2.0f;
        for (int h = 0; h < 6; h++) {
            Color hc = (h < s->lives_all[pi]) ? TEXT_BAD : (Color){50,50,80,255};
            draw_heart(hstart + h*20, py+ph-28, 7, hc);
        }

        // Eliminat overlay
        if (s->elim_all[pi]) {
            DrawRectangle(px, py, panel_w, ph, (Color){0,0,0,130});
            int etw = MeasureText("ELIMINAT", 28);
            DrawText("ELIMINAT", px+panel_w/2-etw/2, py+ph/2-14, 28, TEXT_BAD);
        }
    }

    // ── Zona jos: cuvant + greselile mele + mesaj + indiciu ──────────────────
    int bpy = 440;
    DrawRectangleRounded((Rectangle){10,bpy,SCREEN_W-20,SCREEN_H-bpy-10}, 0.05f, 8, PANEL_COLOR);

    // Cuvant
    int dtw = MeasureText(s->display, 38);
    DrawText(s->display, SCREEN_W/2-dtw/2, bpy+8, 38, TEXT_MAIN);

    // Greselile mele
    if (strlen(s->my_wrong) > 0) {
        DrawText("Greselile mele:", 20, bpy+58, 18, TEXT_DIM);
        DrawText(s->my_wrong, 175, bpy+56, 22, TEXT_BAD);
    }

   // Indicii (doar daca e randul meu)
    if (!s->elim_all[s->my_idx] && s->current_turn == s->my_idx && !s->game_over) {
        int hints_left = 3 - s->hints_used;
        char hbuf[48];
        snprintf(hbuf, 48, "? Indiciu (%d ramase)", hints_left);
        Color hc = hints_left > 0 ? ACCENT2 : TEXT_DIM;
        int htw = MeasureText(hbuf, 20);
        Rectangle hrect = {SCREEN_W-htw-40, bpy+50, htw+30, 36};
        
        // --- LOGICA REPARATĂ PENTRU CLICK HINT MULTIPLAYER ---
        bool h_hovered = CheckCollisionPointRec(GetMousePosition(), hrect);
        DrawRectangleRounded(hrect, 0.3f, 6,
            h_hovered ? BTN_HOVER : (hints_left > 0 ? BTN_NORMAL : (Color){30,20,30,255}));
        DrawRectangleRoundedLines(hrect, 0.3f, 6, h_hovered ? ACCENT1 : hc);
        DrawText(hbuf, (int)(hrect.x+15), (int)(hrect.y+8), 20, hc);

        if (h_hovered && IsMouseButtonPressed(MOUSE_LEFT_BUTTON) && hints_left > 0 && mp_sock >= 0) {
            send(mp_sock, "{\"hint\":1}\n", 11, 0);
        }
    }

    // Mesaj
    int mtw = MeasureText(s->message, 19);
    DrawText(s->message, SCREEN_W/2-mtw/2, bpy+98, 19, TEXT_WARN);

    // Status
  // ── ZONA DE STATUS (ÎNLOCUIEȘTE BLOCUL PROBLEMĂ CU ACESTA) ──
    const char *status_txt;
    Color status_c;
    
    if (s->elim_all[s->my_idx]) {
        status_txt = "Eliminat - urmaresti jocul"; 
        status_c = TEXT_BAD;
    } else if (s->current_turn == s->my_idx) {
        status_txt = "Apasa o litera sau ? pentru indiciu!"; 
        status_c = TEXT_GOOD;
    } else {
        status_txt = "Asteapta randul tau..."; 
        status_c = TEXT_DIM;
    }
    
    int stw = MeasureText(status_txt, 18);
    DrawText(status_txt, SCREEN_W/2 - stw/2, bpy+125, 18, status_c);

    draw_button(menu_btn, 18);

    // ── Game over overlay ────────────────────────────────────────────────────
    if (s->game_over) {
        DrawRectangle(0, 0, SCREEN_W, SCREEN_H, (Color){0, 0, 0, 180});
        if (s->game_over == 1) {
            if (s->winner == s->my_idx) {
                int gtw = MeasureText("AI CASTIGAT!", 80);
                DrawText("AI CASTIGAT!", SCREEN_W/2 - gtw/2, SCREEN_H/2 - 80, 80, TEXT_GOOD);
            } else {
                char wm[64];
                snprintf(wm, 64, "Jucator %d a castigat!", s->winner + 1);
                int gtw = MeasureText(wm, 55);
                DrawText(wm, SCREEN_W/2 - gtw/2, SCREEN_H/2 - 70, 55, player_colors[s->winner % 3]);
                gtw = MeasureText("Ai pierdut!", 40);
                DrawText("Ai pierdut!", SCREEN_W/2 - gtw/2, SCREEN_H/2 + 10, 40, TEXT_BAD);
            }
        } else {
            int gtw = MeasureText("TOTI ELIMINATI!", 70);
            DrawText("TOTI ELIMINATI!", SCREEN_W/2 - gtw/2, SCREEN_H/2 - 60, 70, TEXT_BAD);
        }
        if (strlen(s->secret) > 0) {
            char sw[128];
            snprintf(sw, 128, "Cuvantul era: %s", s->secret);
            int stw2 = MeasureText(sw, 30);
            DrawText(sw, SCREEN_W/2 - stw2/2, SCREEN_H/2 + 80, 30, TEXT_WARN);
        }
        int etw = MeasureText("Apasa ESC pentru meniu", 22);
        DrawText("Apasa ESC pentru meniu", SCREEN_W/2 - etw/2, SCREEN_H/2 + 130, 22, TEXT_DIM);
    }
}

// ─── MAIN ─────────────────────────────────────────────────────────────────────
int main(void) {
    load_words("cuvinte.txt");
    init_stars();

    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_W, SCREEN_H, "Hangman Multiplayer");
    SetTargetFPS(60);

    GameScreen screen = SCREEN_MENU;
    SPGame sp;
    memset(&sp, 0, sizeof(sp));

    char ip_buf[64] = "127.0.0.1";
    int  ip_editing = 0;

    // Butoane meniu
    Button btn_sp   = make_button(SCREEN_W/2-200, 280, 400, 70, "Single Player", BTN_NORMAL, ACCENT3);
    Button btn_mp   = make_button(SCREEN_W/2-200, 370, 400, 70, "Multiplayer",   BTN_NORMAL, ACCENT1);

    // Butoane dificultate
    Button btn_easy = make_button(SCREEN_W/2-470+30, 450, 220, 50, "Joaca EASY",   BTN_NORMAL, EASY_COLOR);
    Button btn_med  = make_button(SCREEN_W/2-130+30, 450, 220, 50, "Joaca MEDIUM", BTN_NORMAL, MEDIUM_COLOR);
    Button btn_hard = make_button(SCREEN_W/2+210+30, 450, 220, 50, "Joaca HARD",   BTN_NORMAL, HARD_COLOR);
    Button btn_back = make_button(10, SCREEN_H-60, 160, 44, "< Inapoi", BTN_NORMAL, TEXT_DIM);

    // Butoane SP
    Button btn_hint = make_button(460, 580, 280, 55, "? Indiciu", BTN_NORMAL, ACCENT2);
    Button btn_sp_menu = make_button(10, SCREEN_H-60, 160, 44, "< Meniu", BTN_NORMAL, TEXT_DIM);

    // Butoane MP
    Button btn_conn    = make_button(SCREEN_W/2-160, 360, 320, 60, "Conecteaza-te", BTN_NORMAL, ACCENT1);
    Button btn_mp_menu = make_button(10, SCREEN_H-60, 160, 44, "< Meniu", BTN_NORMAL, TEXT_DIM);

    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        // ── Update butoane ──
        update_button(&btn_sp);   update_button(&btn_mp);
        update_button(&btn_easy); update_button(&btn_med); update_button(&btn_hard);
        update_button(&btn_back);
        update_button(&btn_hint); update_button(&btn_sp_menu);
        update_button(&btn_conn); update_button(&btn_mp_menu);

        // ── Input global ──
        if (IsKeyPressed(KEY_ESCAPE)) {
            if (screen != SCREEN_MENU) {
                if (mp_sock >= 0) { close(mp_sock); mp_sock = -1; }
                memset(&mp_state, 0, sizeof(mp_state));
                screen = SCREEN_MENU;
            }
        }

        // ── Logica per screen ──
        if (screen == SCREEN_MENU) {
            if (button_clicked(&btn_sp)) screen = SCREEN_DIFFICULTY;
            if (button_clicked(&btn_mp)) screen = SCREEN_MULTIPLAYER;
        }
        else if (screen == SCREEN_DIFFICULTY) {
            if (button_clicked(&btn_back)) screen = SCREEN_MENU;
            if (button_clicked(&btn_easy)) { sp_init(&sp, DIFF_EASY);   screen = SCREEN_SINGLEPLAYER; }
            if (button_clicked(&btn_med))  { sp_init(&sp, DIFF_MEDIUM); screen = SCREEN_SINGLEPLAYER; }
            if (button_clicked(&btn_hard)) { sp_init(&sp, DIFF_HARD);   screen = SCREEN_SINGLEPLAYER; }
        }
        else if (screen == SCREEN_SINGLEPLAYER) {
            if (button_clicked(&btn_sp_menu)) screen = SCREEN_MENU;

            if (!sp.game_over) {
                // timer hard
                if (sp.timed) {
                    sp.time_left -= dt;
                    if (sp.time_left <= 0) { sp.time_left = 0; sp.game_over = 2; }
                }
                // hint
                if (button_clicked(&btn_hint) && sp.diff != DIFF_EASY)
                    sp_use_hint(&sp);
                // input taste
                int ch;
                while ((ch = GetCharPressed()) != 0) {
                    if ((ch>='a'&&ch<='z') || (ch>='A'&&ch<='Z'))
                        sp_guess(&sp, (char)ch);
                }
            } else {
                if (IsKeyPressed(KEY_R)) sp_init(&sp, sp.diff);
                if (IsKeyPressed(KEY_ESCAPE)) screen = SCREEN_MENU;
            }
        }
        else if (screen == SCREEN_MULTIPLAYER) {
            if (button_clicked(&btn_mp_menu)) {
                if (mp_sock >= 0) { close(mp_sock); mp_sock = -1; }
                memset(&mp_state, 0, sizeof(mp_state));
                screen = SCREEN_MENU;
            }

            // IP editing
            if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                Rectangle ip_chk = {SCREEN_W/2-300, 220, 600, 70};
                ip_editing = CheckCollisionPointRec(GetMousePosition(), ip_chk);
            }

            if (ip_editing) {
                int ch;
                while ((ch = GetCharPressed()) != 0) {
                    int l = strlen(ip_buf);
                    if ((ch>='0'&&ch<='9') || ch=='.') {
                        if (l < 63) { ip_buf[l]=ch; ip_buf[l+1]='\0'; }
                    }
                }
                if (IsKeyPressed(KEY_BACKSPACE) && strlen(ip_buf)>0)
                    ip_buf[strlen(ip_buf)-1]='\0';
            }

            if (button_clicked(&btn_conn) && mp_sock < 0) {
                mp_sock = socket(AF_INET, SOCK_STREAM, 0);
                struct sockaddr_in sa = {.sin_family=AF_INET, .sin_port=htons(PORT)};
                inet_pton(AF_INET, ip_buf, &sa.sin_addr);
                if (connect(mp_sock,(struct sockaddr*)&sa,sizeof(sa)) < 0) {
                    close(mp_sock); mp_sock = -1;
                } else {
                    pthread_t rt;
                    pthread_create(&rt, NULL, recv_thread, NULL);
                    pthread_detach(rt);
                }
            }

            if (mp_sock >= 0 && !mp_state.game_over) {
                int ch;
                while ((ch = GetCharPressed()) != 0) {
                    char letter = 0;
                    if (ch>='a'&&ch<='z') letter=ch;
                    if (ch>='A'&&ch<='Z') letter=ch+32;
                    if (letter) {
                        char msg[64];
                        snprintf(msg,64,"{\"letter\":\"%c\"}\n",letter);
                        send(mp_sock,msg,strlen(msg),0);
                    }
                }
            }
        }

        // ── Render ──
        BeginDrawing();
        ClearBackground(BG_COLOR);
        draw_stars();

        if (screen == SCREEN_MENU)
            draw_menu(&btn_sp, &btn_mp);
        else if (screen == SCREEN_DIFFICULTY)
            draw_difficulty(&btn_easy, &btn_med, &btn_hard, &btn_back);
        else if (screen == SCREEN_SINGLEPLAYER)
            draw_singleplayer(&sp, &btn_hint, &btn_sp_menu);
        else if (screen == SCREEN_MULTIPLAYER) {
            pthread_mutex_lock(&mp_mutex);
            MPState s_copy = mp_state;
            pthread_mutex_unlock(&mp_mutex);
            draw_multiplayer(&s_copy, ip_buf, ip_editing, &btn_conn, &btn_mp_menu);
        }

        EndDrawing();
    }

    if (mp_sock >= 0) close(mp_sock);
    CloseWindow();
    return 0;
}
