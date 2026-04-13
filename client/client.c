// client/client.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include "raylib.h"
#include "../common/protocol.h"

// ─── Starea primita de la server ───────────────────────────────────────────
typedef struct {
    char display[MAX_WORD_LEN * 2];
    char wrong[27];
    int  wrong_count;
    int  game_over;
    int  player_count;
    char message[128];
} UIState;

UIState ui_state;
int     sock_fd = -1;
pthread_mutex_t ui_mutex = PTHREAD_MUTEX_INITIALIZER;

// ─── Parsare JSON simplu ───────────────────────────────────────────────────
void parse_state(const char *json) {
    pthread_mutex_lock(&ui_mutex);

    char *p;
    // "display":"..."
    p = strstr(json, "\"display\":\"");
    if (p) { p += 11; sscanf(p, "%[^\"]", ui_state.display); }
    // "wrong":"..."
    p = strstr(json, "\"wrong\":\"");
    if (p) { p += 9;  sscanf(p, "%[^\"]", ui_state.wrong); }
    // "wrong_count":N
    p = strstr(json, "\"wrong_count\":");
    if (p) sscanf(p + 14, "%d", &ui_state.wrong_count);
    // "game_over":N
    p = strstr(json, "\"game_over\":");
    if (p) sscanf(p + 12, "%d", &ui_state.game_over);
    // "players":N
    p = strstr(json, "\"players\":");
    if (p) sscanf(p + 10, "%d", &ui_state.player_count);
    // "msg":"..."
    p = strstr(json, "\"msg\":\"");
    if (p) { p += 7; sscanf(p, "%[^\"]", ui_state.message); }

    pthread_mutex_unlock(&ui_mutex);
}

// ─── Thread de receive ─────────────────────────────────────────────────────
void *recv_thread(void *arg) {
    char buf[BUFFER_SIZE * 4];
    char accum[BUFFER_SIZE * 8] = "";

    while (1) {
        int n = recv(sock_fd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = '\0';
        strncat(accum, buf, sizeof(accum) - strlen(accum) - 1);

        // proceseaza fiecare linie completa (\n)
        char *nl;
        while ((nl = strchr(accum, '\n')) != NULL) {
            *nl = '\0';
            if (strlen(accum) > 2)
                parse_state(accum);
            memmove(accum, nl + 1, strlen(nl + 1) + 1);
        }
    }
    return NULL;
}

// ─── Deseneaza spanzuratoarea ──────────────────────────────────────────────
void draw_hangman(int wrong, int cx, int cy) {
    // Postul
    DrawLine(cx - 60, cy + 120, cx + 60, cy + 120, BLACK); // sol
    DrawLine(cx - 30, cy + 120, cx - 30, cy - 120, BLACK); // stalp
    DrawLine(cx - 30, cy - 120, cx + 40, cy - 120, BLACK); // bara sus
    DrawLine(cx + 40, cy - 120, cx + 40, cy - 90,  BLACK); // bara dreapta
    // Cap
    if (wrong >= 1)
        DrawCircleLines(cx + 40, cy - 75, 15, RED);
    // Corp
    if (wrong >= 2)
        DrawLine(cx + 40, cy - 60, cx + 40, cy, DARKGRAY);
    // Brat stang
    if (wrong >= 3)
        DrawLine(cx + 40, cy - 50, cx + 15, cy - 20, DARKGRAY);
    // Brat drept
    if (wrong >= 4)
        DrawLine(cx + 40, cy - 50, cx + 65, cy - 20, DARKGRAY);
    // Picior stang
    if (wrong >= 5)
        DrawLine(cx + 40, cy,     cx + 15, cy + 30, DARKGRAY);
    // Picior drept
    if (wrong >= 6)
        DrawLine(cx + 40, cy,     cx + 65, cy + 30, DARKGRAY);
}

// ─── Main ──────────────────────────────────────────────────────────────────
int main(int argc, char *argv[]) {
    const char *server_ip = (argc > 1) ? argv[1] : "127.0.0.1";

    // Conectare TCP
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    struct sockaddr_in saddr = {
        .sin_family = AF_INET,
        .sin_port   = htons(PORT)
    };
    inet_pton(AF_INET, server_ip, &saddr.sin_addr);

    if (connect(sock_fd, (struct sockaddr *)&saddr, sizeof(saddr)) < 0) {
        printf("Nu ma pot conecta la %s:%d\n", server_ip, PORT);
        return 1;
    }
    printf("Conectat la server %s:%d\n", server_ip, PORT);

    // Pornire thread receive
    pthread_t rt;
    pthread_create(&rt, NULL, recv_thread, NULL);
    pthread_detach(rt);

    // ─── Raylib init ──────────────────────────────────────────────────────
    InitWindow(800, 600, "Hangman Multiplayer");
    SetTargetFPS(60);

    

    while (!WindowShouldClose()) {
        // ─── Input ────────────────────────────────────────────────────────
       int ch;
while ((ch = GetCharPressed()) != 0) {
    if (ch >= 'a' && ch <= 'z') {
        char msg[64];
        snprintf(msg, 64, "{\"letter\":\"%c\"}\n", (char)ch);
        send(sock_fd, msg, strlen(msg), 0);
    }
    if (ch >= 'A' && ch <= 'Z') {
        char msg[64];
        snprintf(msg, 64, "{\"letter\":\"%c\"}\n", (char)(ch + 32));
        send(sock_fd, msg, strlen(msg), 0);
    }
}
        if (ch >= 'a' && ch <= 'z') {
            char msg[64];
            snprintf(msg, 64, "{\"letter\":\"%c\"}\n", (char)ch);
            send(sock_fd, msg, strlen(msg), 0);
        }
        // convert uppercase la lowercase
        if (ch >= 'A' && ch <= 'Z') {
            char msg[64];
            snprintf(msg, 64, "{\"letter\":\"%c\"}\n", (char)(ch + 32));
            send(sock_fd, msg, strlen(msg), 0);
        }

        // ─── Render ───────────────────────────────────────────────────────
        BeginDrawing();
        ClearBackground(RAYWHITE);

        pthread_mutex_lock(&ui_mutex);
        UIState s = ui_state;
        pthread_mutex_unlock(&ui_mutex);

        // Titlu
        DrawText("HANGMAN MULTIPLAYER", 250, 20, 28, DARKBLUE);

        // Info jucatori
        char info[64];
        snprintf(info, 64, "Jucatori conectati: %d", s.player_count);
        DrawText(info, 20, 60, 18, GRAY);

        // Spanzuratoare (centrata la x=150)
        draw_hangman(s.wrong_count, 150, 280);

        // Cuvant afisat
        DrawText(s.display, 320, 200, 36, BLACK);

        // Litere gresite
        DrawText("Litere gresite:", 320, 280, 20, DARKGRAY);
        DrawText(s.wrong, 320, 310, 24, RED);

        // Vieti ramase (inimute)
        char lives[32];
        snprintf(lives, 32, "Vieti: %d / %d",
                 MAX_WRONG_GUESSES - s.wrong_count, MAX_WRONG_GUESSES);
        DrawText(lives, 320, 350, 20, MAROON);

        // Mesaj de la server
        DrawText(s.message, 50, 490, 20, DARKGREEN);

        // Game over overlay
        if (s.game_over == 1) {
            DrawRectangle(0, 0, 800, 600, Fade(GREEN, 0.3f));
            DrawText("AI CASTIGAT!", 270, 260, 48, DARKGREEN);
        } else if (s.game_over == 2) {
            DrawRectangle(0, 0, 800, 600, Fade(RED, 0.3f));
            DrawText("AI PIERDUT!", 280, 260, 48, MAROON);
        }

        // instructiune
        DrawText("Apasa o litera pentru a ghici", 240, 560, 16, LIGHTGRAY);

        EndDrawing();
    }

    CloseWindow();
    close(sock_fd);
    return 0;
}
