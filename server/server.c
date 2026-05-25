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

#define PORT         8080
#define MAX_PLAYERS  3
#define MAX_WORD_LEN 64
#define BUFFER_SIZE  1024
#define MAX_LIVES    6
#define MAX_HINTS    3

typedef struct {
    int  sock;
    int  lives;
    int  eliminated;
    int  active;
    int  wrong_count;    // greselile individuale
    char wrong[27];      // literele gresite individual
    int  hints_used;     // cate indicii a folosit
} Player;

typedef struct {
    char secret[MAX_WORD_LEN];
    char hints[MAX_HINTS][256];  // 3 indicii per cuvant
    int  guessed[26];            // litere ghicite corect (global)
    int  game_over;
    int  waiting;
    int  current_turn;
    int  player_count;
    int  winner;
    Player players[MAX_PLAYERS];
} Game;

Game game;
pthread_mutex_t gmux = PTHREAD_MUTEX_INITIALIZER;

// ─── Incarca cuvinte si indicii ───────────────────────────────────────────────
typedef struct {
    char word[MAX_WORD_LEN];
    char hints[MAX_HINTS][256];
} WordEntry;

WordEntry entries[300];
int entry_count = 0;

void load_words_and_hints() {
    // Incarca cuvinte
    FILE *fw = fopen("cuvinte.txt", "r");
    char word_list[300][MAX_WORD_LEN];
    int wcount = 0;
    if (fw) {
        while (fscanf(fw, "%s", word_list[wcount]) == 1 && wcount < 300) {
            for (int i = 0; word_list[wcount][i]; i++)
                word_list[wcount][i] = tolower(word_list[wcount][i]);
            wcount++;
        }
        fclose(fw);
    }

    // Incarca indicii
    FILE *fh = fopen("indicii.txt", "r");
    char hint_map[300][MAX_WORD_LEN];
    char hint_vals[300][MAX_HINTS][256];
    int hcount = 0;

    if (fh) {
        char line[1024];
        while (fgets(line, sizeof(line), fh) && hcount < 300) {
            // format: cuvant|indiciu1|indiciu2|indiciu3
            line[strcspn(line, "\n")] = '\0';
            char *token = strtok(line, "|");
            if (!token) continue;
            strncpy(hint_map[hcount], token, MAX_WORD_LEN-1);
            for (int i = 0; i < MAX_HINTS; i++) {
                token = strtok(NULL, "|");
                if (token)
                    strncpy(hint_vals[hcount][i], token, 255);
                else
                    snprintf(hint_vals[hcount][i], 256,
                             "Cuvant cu %d litere.", (int)strlen(hint_map[hcount]));
            }
            hcount++;
        }
        fclose(fh);
    }

    // Combina
    entry_count = 0;
    for (int w = 0; w < wcount; w++) {
        strcpy(entries[entry_count].word, word_list[w]);
        // cauta indicii pentru acest cuvant
        int found = 0;
        for (int h = 0; h < hcount; h++) {
            if (strcmp(hint_map[h], word_list[w]) == 0) {
                for (int i = 0; i < MAX_HINTS; i++)
                    strcpy(entries[entry_count].hints[i], hint_vals[h][i]);
                found = 1;
                break;
            }
        }
        if (!found) {
            // indicii generice
            snprintf(entries[entry_count].hints[0], 256,
                     "Cuvant cu %d litere.", (int)strlen(word_list[w]));
            snprintf(entries[entry_count].hints[1], 256,
                     "Incepe cu litera '%c'.", word_list[w][0]);
            snprintf(entries[entry_count].hints[2], 256,
                     "Se termina cu litera '%c'.",
                     word_list[w][strlen(word_list[w])-1]);
        }
        entry_count++;
    }

    if (entry_count == 0) {
        // fallback
        char def[][MAX_WORD_LEN] = {
            "programare","algoritm","calculator","retea","compilator"
        };
        entry_count = 5;
        for (int i = 0; i < entry_count; i++) {
            strcpy(entries[i].word, def[i]);
            snprintf(entries[i].hints[0], 256, "Cuvant din informatica.");
            snprintf(entries[i].hints[1], 256, "Are %d litere.", (int)strlen(def[i]));
            snprintf(entries[i].hints[2], 256, "Incepe cu '%c'.", def[i][0]);
        }
    }
    printf("[SERVER] %d cuvinte cu indicii incarcate.\n", entry_count);
}

void init_game() {
    memset(game.guessed, 0, sizeof(game.guessed));
    game.game_over    = 0;
    game.waiting      = 1;
    game.current_turn = 0;
    game.winner       = -1;

    srand(time(NULL));
    int idx = rand() % entry_count;
    strcpy(game.secret, entries[idx].word);
    for (int i = 0; i < MAX_HINTS; i++)
        strcpy(game.hints[i], entries[idx].hints[i]);

    for (int i = 0; i < MAX_PLAYERS; i++) {
        game.players[i].lives       = MAX_LIVES;
        game.players[i].eliminated  = 0;
        game.players[i].wrong_count = 0;
        game.players[i].hints_used  = 0;
        memset(game.players[i].wrong, 0, sizeof(game.players[i].wrong));
    }
    printf("[SERVER] Cuvant ales: %s\n", game.secret);
}

void build_display(char *out) {
    int pos = 0;
    for (int i = 0; game.secret[i]; i++) {
        char c = game.secret[i];
        int idx = c - 'a';
        if (idx >= 0 && idx <= 25 && game.guessed[idx])
            out[pos++] = c;
        else
            out[pos++] = '_';
        out[pos++] = ' ';
    }
    if (pos > 0) pos--;
    out[pos] = '\0';
}

int word_complete() {
    for (int i = 0; game.secret[i]; i++) {
        int idx = game.secret[i] - 'a';
        if (idx >= 0 && idx <= 25 && !game.guessed[idx])
            return 0;
    }
    return 1;
}

int active_count() {
    int n = 0;
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (game.players[i].active && !game.players[i].eliminated)
            n++;
    return n;
}

void advance_turn() {
    int tries = MAX_PLAYERS;
    do {
        game.current_turn = (game.current_turn + 1) % MAX_PLAYERS;
        tries--;
    } while (tries > 0 &&
             (!game.players[game.current_turn].active ||
              game.players[game.current_turn].eliminated));
}

// ─── Trimite stare ────────────────────────────────────────────────────────────
void send_state(int pidx, const char *msg) {
    if (!game.players[pidx].active || game.players[pidx].sock <= 0) return;

    char disp[MAX_WORD_LEN * 2];
    build_display(disp);

    // lives_all si wrong_count_all si elim_all per jucator
    char lives_str[16] = "", wcount_str[16] = "", elim_str[16] = "";
    for (int i = 0; i < MAX_PLAYERS; i++) {
        char tmp[8];
        snprintf(tmp, 8, "%d", game.players[i].lives);
        if (i > 0) { strncat(lives_str,  ",", 15); strncat(wcount_str, ",", 15); strncat(elim_str, ",", 15); }
        strncat(lives_str, tmp, 15);
        snprintf(tmp, 8, "%d", game.players[i].wrong_count);
        strncat(wcount_str, tmp, 15);
        snprintf(tmp, 4, "%d", game.players[i].eliminated);
        strncat(elim_str, tmp, 15);
    }

    // wrong individual al jucatorului cerut
    char buf[BUFFER_SIZE];
    snprintf(buf, BUFFER_SIZE,
        "{\"display\":\"%s\",\"my_wrong\":\"%s\","
        "\"game_over\":%d,\"players\":%d,\"msg\":\"%s\","
        "\"turn\":%d,\"my_idx\":%d,\"waiting\":%d,"
        "\"winner\":%d,\"secret\":\"%s\","
        "\"lives_all\":\"%s\",\"wcount_all\":\"%s\","
        "\"elim_all\":\"%s\",\"hints_used\":%d}\n",
        disp,
        game.players[pidx].wrong,
        game.game_over, game.player_count, msg,
        game.current_turn, pidx, game.waiting,
        game.winner,
        (game.game_over > 0) ? game.secret : "",
        lives_str, wcount_str, elim_str,
        game.players[pidx].hints_used
    );
    send(game.players[pidx].sock, buf, strlen(buf), 0);
}

void broadcast(const char *msg) {
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (game.players[i].active)
            send_state(i, msg);
}

// ─── Procesare guess ─────────────────────────────────────────────────────────
void process_guess(int pidx, char letter) {
    if (game.game_over || game.waiting) return;
    if (pidx != game.current_turn) {
        send_state(pidx, "Nu este randul tau!");
        return;
    }
    Player *p = &game.players[pidx];
    if (p->eliminated) { send_state(pidx, "Esti eliminat!"); return; }

    letter = tolower(letter);
    int idx = letter - 'a';
    if (idx < 0 || idx > 25) return;

    char msg[128];

    // litera deja ghicita corect global
    if (game.guessed[idx]) {
        snprintf(msg, 128, "Litera '%c' deja ghicita corect!", letter);
        send_state(pidx, msg);
        return;
    }
    // litera deja incercata de acest jucator
    if (strchr(p->wrong, letter)) {
        snprintf(msg, 128, "Ai mai incercat '%c'!", letter);
        send_state(pidx, msg);
        return;
    }

    if (strchr(game.secret, letter)) {
        game.guessed[idx] = 1;
        snprintf(msg, 128, "Jucator %d: '%c' corect!", pidx+1, letter);
        if (word_complete()) {
            game.game_over = 1;
            game.winner    = pidx;
            char wm[128];
            snprintf(wm, 128, "Jucator %d a ghicit: %s! Ceilalti pierd.", pidx+1, game.secret);
            broadcast(wm);
            return;
        }
    } else {
        // GRESIT - doar jucatorul curent pierde o viata
        int wlen = strlen(p->wrong);
        p->wrong[wlen]   = letter;
        p->wrong[wlen+1] = '\0';
        p->wrong_count++;
        p->lives--;

        snprintf(msg, 128, "Jucator %d: '%c' gresit! Vieti: %d",
                 pidx+1, letter, p->lives);

        if (p->lives <= 0) {
            p->eliminated = 1;
            snprintf(msg, 128, "Jucator %d eliminat!", pidx+1);
            broadcast(msg);
            if (active_count() == 0) {
                game.game_over = 2;
                char lm[128];
                snprintf(lm, 128, "Toti eliminati! Cuvantul era: %s", game.secret);
                broadcast(lm);
                return;
            }
            if (game.current_turn == pidx) advance_turn();
            broadcast(msg);
            return;
        }
    }

    broadcast(msg);
    advance_turn();
    char tm[64];
    snprintf(tm, 64, "Randul Jucatorului %d!", game.current_turn+1);
    broadcast(tm);
}

// ─── Procesare cerere indiciu (VARIANTA CU COST DE VIETI) ──────────────────────
void process_hint(int pidx) {
    if (game.game_over || game.waiting) return;
    if (pidx != game.current_turn) {
        send_state(pidx, "Nu este randul tau pentru indiciu!");
        return;
    }
    Player *p = &game.players[pidx];
    if (p->hints_used >= MAX_HINTS) {
        send_state(pidx, "Ai folosit toate indiciile!");
        return;
    }

    // --- LOGICA NOUĂ: Cost indiciu (ex: 2 vieți) ---
    int HINT_COST = 2; 
    if (p->lives < HINT_COST) {
        send_state(pidx, "Nu ai destule vieti pentru a cere un indiciu!");
        return;
    }

    // Scădem viețile și creștem indiciile folosite
    p->lives -= HINT_COST;
    p->wrong_count += HINT_COST; // Pentru ca spanzuratoarea din client sa se deseneze corect

    char msg[300];
    snprintf(msg, 300, "Indiciu %d: %s",
             p->hints_used+1, game.hints[p->hints_used]);
    p->hints_used++;

    // Trimite indiciul doar jucătorului care l-a cerut
    send_state(pidx, msg);

    // Anunță ceilalți că jucătorul a cerut un indiciu
    char notif[128];
    snprintf(notif, 128, "Jucator %d a cerut un indiciu (-%d vieti).", pidx+1, HINT_COST);
    for (int i = 0; i < MAX_PLAYERS; i++)
        if (i != pidx && game.players[i].active)
            send_state(i, notif);

    // Verificăm dacă jucătorul s-a eliminat singur cerând indiciul
    if (p->lives <= 0) {
        p->eliminated = 1;
        char elim_msg[128];
        snprintf(elim_msg, 128, "Jucator %d s-a eliminat cerand indiciu!", pidx+1);
        broadcast(elim_msg);
        
        if (active_count() == 0) {
            game.game_over = 2;
            char lm[128];
            snprintf(lm, 128, "Toti eliminati! Cuvantul era: %s", game.secret);
            broadcast(lm);
            return;
        }
        if (game.current_turn == pidx) advance_turn();
        broadcast(elim_msg);
        return;
    }

    // Dacă nu s-a eliminat, starea actualizată (cu mai puține vieți) va fi trimisă oricum prin broadcast-ul de la final de tură dacă schimbi tura, 
    // sau poți face un broadcast simplu aici ca să vadă toți noile vieți:
    for (int i = 0; i < MAX_PLAYERS; i++) {
        if (i != pidx && game.players[i].active) {
            char notif_lives[128];
            snprintf(notif_lives, 128, "Jucator %d a cerut un indiciu (-%d vieti).", pidx+1, HINT_COST);
            send_state(i, notif_lives);
        }
    }
    
    // Iar pentru TINE, re-trimitem starea cu indiciul ca să fim siguri că rămâne fixat pe ecran
    send_state(pidx, msg);
}


// ─── Thread client ────────────────────────────────────────────────────────────
typedef struct { int sock; int idx; } CArgs;

void *handle_client(void *arg) {
    CArgs *ca = (CArgs*)arg;
    int sock = ca->sock, idx = ca->idx;
    free(ca);
    char buf[BUFFER_SIZE];

    pthread_mutex_lock(&gmux);
    char jm[128];
    snprintf(jm, 128, "Jucator %d conectat! %s", idx+1,
             game.player_count < 2 ? "Asteptam inca un jucator..." : "Gata!");
    broadcast(jm);
    if (game.player_count >= 2 && game.waiting) {
        game.waiting      = 0;
        game.current_turn = 0;
        while (!game.players[game.current_turn].active)
            game.current_turn = (game.current_turn+1) % MAX_PLAYERS;
        char sm[64];
        snprintf(sm, 64, "Joc inceput! Randul Jucatorului %d!", game.current_turn+1);
        broadcast(sm);
    }
    pthread_mutex_unlock(&gmux);

    while (1) {
        int n = recv(sock, buf, sizeof(buf)-1, 0);
        if (n <= 0) break;
        buf[n] = '\0';

        char *p = strstr(buf, "\"letter\":\"");
        if (p) {
            char letter = p[10];
            if (isalpha(letter)) {
                pthread_mutex_lock(&gmux);
                process_guess(idx, letter);
                pthread_mutex_unlock(&gmux);
            }
        }
        // cerere indiciu: {"hint":1}
        if (strstr(buf, "\"hint\":1")) {
            pthread_mutex_lock(&gmux);
            process_hint(idx);
            pthread_mutex_unlock(&gmux);
        }
    }

    pthread_mutex_lock(&gmux);
    game.players[idx].active     = 0;
    game.players[idx].eliminated = 1;
    game.players[idx].sock       = 0;
    game.player_count--;
    if (active_count() == 0 && !game.game_over) game.game_over = 2;
    else if (game.current_turn == idx && active_count() > 0) advance_turn();
    broadcast("Un jucator s-a deconectat.");
    pthread_mutex_unlock(&gmux);
    close(sock);
    return NULL;
}

int main() {
    load_words_and_hints();
    init_game();

    int sfd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(sfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    struct sockaddr_in addr = {
        .sin_family=AF_INET, .sin_addr.s_addr=INADDR_ANY, .sin_port=htons(PORT)
    };
    bind(sfd, (struct sockaddr*)&addr, sizeof(addr));
    listen(sfd, MAX_PLAYERS);
    printf("[SERVER] Asculta pe portul %d...\n", PORT);

    while (1) {
        struct sockaddr_in ca;
        socklen_t len = sizeof(ca);
        int nsock = accept(sfd, (struct sockaddr*)&ca, &len);
        if (nsock < 0) continue;

        pthread_mutex_lock(&gmux);
        if (game.player_count >= MAX_PLAYERS) {
            send(nsock, "{\"msg\":\"Server plin!\"}\n", 22, 0);
            close(nsock);
            pthread_mutex_unlock(&gmux);
            continue;
        }
        int idx = 0;
        while (idx < MAX_PLAYERS && game.players[idx].active) idx++;
        game.players[idx].sock       = nsock;
        game.players[idx].lives      = MAX_LIVES;
        game.players[idx].eliminated = 0;
        game.players[idx].active     = 1;
        game.players[idx].wrong_count= 0;
        game.players[idx].hints_used = 0;
        memset(game.players[idx].wrong, 0, 27);
        game.player_count++;
        printf("[SERVER] Jucator %d conectat: %s\n", idx+1, inet_ntoa(ca.sin_addr));
        pthread_mutex_unlock(&gmux);

        CArgs *carg = malloc(sizeof(CArgs));
        carg->sock = nsock; carg->idx = idx;
        pthread_t t;
        pthread_create(&t, NULL, handle_client, carg);
        pthread_detach(t);
    }
    return 0;
}
