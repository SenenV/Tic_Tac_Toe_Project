#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <mosquitto.h>

#define MQTT_HOST   "localhost"
#define MQTT_PORT   1883
#define MQTT_KEEP   60

#define TOP_MOVE    "ttt/player/move"
#define TOP_CONTROL "ttt/game/control"
#define TOP_STATE   "ttt/game/state"
#define TOP_STATUS  "ttt/game/status"

#define EMPTY   ' '
#define PLAYER_X 'X'
#define PLAYER_O 'O'

#define MODE_NONE 0
#define MODE_1P   1
#define MODE_2P   2

char board[9];
char current_player;
int  game_mode;
int  game_active;

struct mosquitto *mosq;

/* resets board back to empty state */
void reset_board() {
    for (int i = 0; i < 9; i++) board[i] = EMPTY;
    current_player = PLAYER_X;
    game_active = 0;
}

/* sends current board to broker */
void publish_board() {
    char state[10];

    for (int i = 0; i < 9; i++) state[i] = board[i];

    state[9] = '\0';

    mosquitto_publish(mosq, NULL, TOP_STATE, 9, state, 0, false);

    printf("Board: [%s]\n", state);
}

/* publish status messages (winner/draw/turn) */
void publish_status(const char *msg) {
    mosquitto_publish(mosq, NULL, TOP_STATUS, strlen(msg), msg, 0, false);

    printf("Status: %s\n", msg);
}

/* prints board to terminal (debugging)*/
void print_board() {
    printf("\n");

    printf(" %c | %c | %c\n", board[0], board[1], board[2]);
    printf("---+---+---\n");
    printf(" %c | %c | %c\n", board[3], board[4], board[5]);
    printf("---+---+---\n");
    printf(" %c | %c | %c\n", board[6], board[7], board[8]);

    printf("\n");
}

/* checks for winning combinations */
int check_winner(char sym) {
    if (board[0]==sym && board[1]==sym && board[2]==sym) return 1;
    if (board[3]==sym && board[4]==sym && board[5]==sym) return 1;
    if (board[6]==sym && board[7]==sym && board[8]==sym) return 1;

    if (board[0]==sym && board[3]==sym && board[6]==sym) return 1;
    if (board[1]==sym && board[4]==sym && board[7]==sym) return 1;
    if (board[2]==sym && board[5]==sym && board[8]==sym) return 1;

    if (board[0]==sym && board[4]==sym && board[8]==sym) return 1;
    if (board[2]==sym && board[4]==sym && board[6]==sym) return 1;

    return 0;
}

/* checks to see if board is full*/
int check_draw() {
    for (int i = 0; i < 9; i++) {
        if (board[i] == EMPTY) return 0;
    }

    return 1;
}

/* notifies the turn of the players */
void notify_turn() {
    publish_status("your_turn");

    printf("It is %c's turn\n", current_player);
}

/* handle player moves */
void handle_move(const char *msg) {
    
    /* ignore moves once game ends */
    if (!game_active) {
        printf("Move received but no game active, ignoring.\n");
        return;
    }
    /*  validates move format (X:5) */
    if (strlen(msg) < 3 || msg[1] != ':') {
        printf("Invalid move format: %s\n", msg);
        return;
    }

    char sym = msg[0];
    int  pos = msg[2] - '0';

    /* makes sure its the correct players turn */
    if (sym != current_player) {
        publish_status("invalid_not_your_turn");

        printf("Wrong player: got %c, expected %c\n",
               sym, current_player);

        return;
    }

    /* valid board range */
    if (pos < 1 || pos > 9) {
        publish_status("invalid_position_taken");

        printf("Position out of range: %d\n", pos);

        return;
    }

    int idx = pos - 1;

    /* prevents overwriting existing moves */
    if (board[idx] != EMPTY) {
        publish_status("invalid_position_taken");

        printf("Position %d already taken\n", pos);

        return;
    }

    board[idx] = sym;

    print_board();

    publish_board();

    /* if current players move won */
    if (check_winner(sym)) {
        char result[16];

        snprintf(result, sizeof(result), "winner:%c", sym);

        publish_status(result);

        printf("Winner: %c\n", sym);

        reset_board();

        return;
    }

    /* if tie */
    if (check_draw()) {
        publish_status("draw");

        printf("Draw!\n");

        reset_board();

        return;
    }

    current_player = (current_player == PLAYER_X)
                     ? PLAYER_O
                     : PLAYER_X;

    notify_turn();
}

/* start, restart commands */
void handle_control(const char *msg) {

    /* single player */
    if (strcmp(msg, "start_1p") == 0) {

        reset_board();

        game_mode   = MODE_1P;
        game_active = 1;

        printf("1-player game started\n");

        publish_board();

        publish_status("game_started_1p");

        notify_turn();

      /* two player */
    } else if (strcmp(msg, "start_2p") == 0) {

        reset_board();

        game_mode   = MODE_2P;
        game_active = 1;

        printf("2-player game started\n");

        publish_board();

        publish_status("game_started_2p");

        notify_turn();

      /* quit command */
    } else if (strcmp(msg, "quit") == 0) {

        printf("Quit received, resetting.\n");

        reset_board();

        publish_status("game_reset");

    } else {

        printf("Unknown control message: %s\n", msg);
    }
}

/* starts when mqtt message is received */
void on_message(struct mosquitto *m,
                void *userdata,
                const struct mosquitto_message *message) {

    if (!message->payloadlen) return;

    char payload[64] = {0};

    int len = message->payloadlen < 63
              ? message->payloadlen
              : 63;

    strncpy(payload, (char *)message->payload, len);

    printf("Received [%s]: %s\n",
           message->topic,
           payload);

    if (strcmp(message->topic, TOP_MOVE) == 0) {

        handle_move(payload);

    } else if (strcmp(message->topic, TOP_CONTROL) == 0) {

        handle_control(payload);
    }
}

/* runs when connecting to broker */
void on_connect(struct mosquitto *m,
                void *userdata,
                int result) {

    if (result == 0) {

        printf("Connected to MQTT broker\n");

        mosquitto_subscribe(m, NULL, TOP_MOVE,    0);
        mosquitto_subscribe(m, NULL, TOP_CONTROL, 0);

        publish_status("waiting_for_players");

    } else {

        printf("MQTT connect failed: %d\n", result);
    }
}

/* main */
int main() {
    printf("Tic-Tac-Toe Game Server starting...\n");

    reset_board();

    game_mode = MODE_NONE;

    mosquitto_lib_init();

    mosq = mosquitto_new("ttt_server", true, NULL);

    if (!mosq) {
        fprintf(stderr,
                "Failed to create mosquitto instance\n");

        return 1;
    }

    mosquitto_connect_callback_set(mosq, on_connect);

    mosquitto_message_callback_set(mosq, on_message);

    int rc = mosquitto_connect(mosq,
                               MQTT_HOST,
                               MQTT_PORT,
                               MQTT_KEEP);

    if (rc != MOSQ_ERR_SUCCESS) {

        fprintf(stderr,
                "Could not connect to broker: %d\n",
                rc);

        return 1;
    }

    printf("Running. Waiting for players...\n");

    mosquitto_loop_forever(mosq, -1, 1);

    mosquitto_destroy(mosq);

    mosquitto_lib_cleanup();

    return 0;
}
