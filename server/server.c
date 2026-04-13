// server/server.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ctype.h>
#include <time.h>
#include "../common/protocol.h"

#define MAX_CLIENTS 4

// ─── Starea globala a jocului ───────────────────────────────────────────────
typedef struct {
    char secret_word[MAX_WORD_LEN];
    int  guessed[26];   // 1 daca litera a-z a fost ghicita corect
    char wrong[27];
    int  wrong_count;
    int  game_over;
} Game;

Game game;
int  client_sockets[MAX_CLIENTS];
int  client_count = 0;
pthread_mutex_t game_mutex = PTHREAD_MUTEX_INITIALIZER;

// ─── Selecteaza un cuvant aleator ──────────────────────────────────────────
void load_random_word(const char *filename) {
    FILE *f = fopen(filename, "r");
    char words[200][MAX_WORD_LEN];
    int count = 0;

    if (f) {
        while (fscanf(f, "%s", words[count]) == 1 && count < 200)
            count++;
        fclose(f);
    }

    // fallback daca fisierul nu exista
    if (count == 0) {
        char defaults[][MAX_WORD_LEN] = {
            "programare","calculator","tastatura","compilator",
            "algoritm","retea","variabila","functie"
        };
        count = 8;
        memcpy(words, defaults, sizeof(defaults));
    }

    srand(time(NULL));
    strcpy(game.secret_word, words[rand() % count]);
    // transforma in lowercase
    for (int i = 0; game.secret_word[i]; i++)
        game.secret_word[i] = tolower(game.secret_word[i]);
}

// ─── Initializeaza jocul ───────────────────────────────────────────────────
void init_game() {
    memset(game.guessed, 0, sizeof(game.guessed));
    memset(game.wrong,   0, sizeof(game.wrong));
    game.wrong_count = 0;
    game.game_over   = 0;
    load_random_word("cuvinte.txt");
    printf("[SERVER] Cuvant ales: %s\n", game.secret_word);
}

// ─── Construieste afisajul "_ _ a _ _" ────────────────────────────────────
void build_display(char *out) {
    int pos = 0;
    for (int i = 0; game.secret_word[i]; i++) {
        if (game.guessed[game.secret_word[i] - 'a'])
            out[pos++] = game.secret_word[i];
        else
            out[pos++] = '_';
        out[pos++] = ' ';
    }
    if (pos > 0) pos--; // sterge spatiul final
    out[pos] = '\0';
}

// ─── Verifica daca cuvantul e complet ghicit ───────────────────────────────
int word_complete() {
     for (int i = 0; game.secret_word[i] != '\0'; i++) {
        char c = game.secret_word[i];
        if (c < 'a' || c > 'z') continue;
        if (game.guessed[c - 'a'] != 1)
            return 0;
    }
    return 1;
}

// ─── Serializeaza GameState -> JSON string ─────────────────────────────────
void serialize_state(GameState *gs, char *buf) {
    snprintf(buf, BUFFER_SIZE,
        "{\"display\":\"%s\",\"wrong\":\"%s\","
        "\"wrong_count\":%d,\"game_over\":%d,"
        "\"players\":%d,\"msg\":\"%s\"}",
        gs->word_display, gs->wrong_letters,
        gs->wrong_count,  gs->game_over,
        gs->player_count, gs->message);
}

// ─── Trimite starea la toti clientii ──────────────────────────────────────
void broadcast_state(const char *msg) {
    GameState gs;
    build_display(gs.word_display);
    strcpy(gs.wrong_letters, game.wrong);
    gs.wrong_count  = game.wrong_count;
    gs.game_over    = game.game_over;
    gs.player_count = client_count;
    strncpy(gs.message, msg, 127);

    char buf[BUFFER_SIZE];
    serialize_state(&gs, buf);
    strcat(buf, "\n"); // delimitator de mesaj

    for (int i = 0; i < client_count; i++) {
        if (client_sockets[i] > 0)
            send(client_sockets[i], buf, strlen(buf), 0);
    }
    printf("[BROADCAST] %s\n", buf);
}

// ─── Proceseaza o litera trimisa de un client ──────────────────────────────
void process_guess(char letter, int client_idx) {
    pthread_mutex_lock(&game_mutex);

    if (game.game_over) {
        pthread_mutex_unlock(&game_mutex);
        return;
    }

    letter = tolower(letter);
    char msg[128];

    // deja ghicita?
    if (game.guessed[letter- 'a'] ||
        strchr(game.wrong, letter)) {
        snprintf(msg, 128, "Litera '%c' deja incercata!", letter);
        broadcast_state(msg);
        pthread_mutex_unlock(&game_mutex);
        return;
    }

    // verifica daca e in cuvant
    if (strchr(game.secret_word, letter)) {
        game.guessed[letter - 'a'] = 1;
        snprintf(msg, 128, "Corect! Litera '%c' este in cuvant.", letter);
        int wc = word_complete();
	if (wc == 1) {
	  game.game_over = 1;
	  snprintf(msg, 128, "Felicitari! Cuvantul era: %s", game.secret_word);
	}
    } else {
       int wlen = strlen(game.wrong);
       if (wlen < 26) {
	 game.wrong[wlen] = letter;
	 game.wrong[wlen + 1] = '\0';  // terminator explicit
       }
        game.wrong_count++;
        snprintf(msg, 128, "Gresit! Litera '%c' nu e in cuvant. (%d/6)",
                 letter, game.wrong_count);
        if (game.wrong_count >= MAX_WRONG_GUESSES) {
            game.game_over = 2;
            snprintf(msg, 128, "Game over! Cuvantul era: %s", game.secret_word);
        }
    }

    broadcast_state(msg);
    pthread_mutex_unlock(&game_mutex);
}

// ─── Thread per client ─────────────────────────────────────────────────────
typedef struct { int sock; int idx; } ClientArgs;

void *handle_client(void *arg) {
    ClientArgs *ca = (ClientArgs *)arg;
    int sock = ca->sock;
    int idx  = ca->idx;
    free(ca);

    char buf[BUFFER_SIZE];

    // trimite starea initiala la noul client
    pthread_mutex_lock(&game_mutex);
    broadcast_state("Jucator nou conectat! Ghiceste litera.");
    pthread_mutex_unlock(&game_mutex);

    while (1) {
        int n = recv(sock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) break;
        buf[n] = '\0';

        // parsare simpla: {"letter":"a"}
        char *p = strstr(buf, "\"letter\":\"");
        if (p) {
            char letter = p[10]; // caracterul dupa "letter":"
            if (isalpha(letter))
                process_guess(letter, idx);
        }
    }

    printf("[SERVER] Client %d deconectat.\n", idx);
    pthread_mutex_lock(&game_mutex);
    client_sockets[idx] = 0;
    client_count--;
    pthread_mutex_unlock(&game_mutex);
    close(sock);
    return NULL;
}

// ─── Main ──────────────────────────────────────────────────────────────────
int main() {
    init_game();

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr = {
        .sin_family      = AF_INET,
        .sin_addr.s_addr = INADDR_ANY,
        .sin_port        = htons(PORT)
    };

    bind(server_fd, (struct sockaddr *)&addr, sizeof(addr));
    listen(server_fd, MAX_CLIENTS);
    printf("[SERVER] Asculta pe portul %d...\n", PORT);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t len = sizeof(client_addr);
        int new_sock = accept(server_fd,
                              (struct sockaddr *)&client_addr, &len);
        if (new_sock < 0) continue;

        pthread_mutex_lock(&game_mutex);
        if (client_count >= MAX_CLIENTS) {
            send(new_sock, "{\"msg\":\"Server plin\"}\n", 22, 0);
            close(new_sock);
            pthread_mutex_unlock(&game_mutex);
            continue;
        }
        // gaseste un slot liber
        int idx = 0;
        while (client_sockets[idx] != 0) idx++;
        client_sockets[idx] = new_sock;
        client_count++;
        printf("[SERVER] Client %d conectat: %s\n",
               idx, inet_ntoa(client_addr.sin_addr));
        pthread_mutex_unlock(&game_mutex);

        ClientArgs *ca = malloc(sizeof(ClientArgs));
        ca->sock = new_sock;
        ca->idx  = idx;
        pthread_t t;
        pthread_create(&t, NULL, handle_client, ca);
        pthread_detach(t);
    }
    return 0;
}
