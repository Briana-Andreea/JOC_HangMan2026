// common/protocol.h
#ifndef PROTOCOL_H
#define PROTOCOL_H

#define PORT 8080
#define MAX_WORD_LEN 64
#define MAX_WRONG_GUESSES 6
#define BUFFER_SIZE 512

// Mesaj de la CLIENT -> SERVER
// Format JSON: {"type":"guess","letter":"a"}
// sau:         {"type":"join","name":"Player1"}

// Mesaj de la SERVER -> CLIENT
typedef struct {
    char word_display[MAX_WORD_LEN * 2]; // "_ _ a _ _"
    char wrong_letters[27];              // "bcd..." literele gresite
    int  wrong_count;                    // 0..6
    int  game_over;                      // 0=in joc, 1=castigat, 2=pierdut
    int  player_count;                   // cati jucatori conectati
    char message[128];                   // "Corect!" / "Gresit!" etc.
} GameState;

#endif
