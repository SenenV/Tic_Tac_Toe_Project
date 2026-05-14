#include "raylib.h"
#include <mosquitto.h>
#include <string.h>
#include <stdio.h>

char MQTT_HOST[128] = "";
#define MQTT_PORT 1883

char board[9] = {' ',' ',' ',' ',' ',' ',' ',' ',' '};
int  my_turn  = 0;
int  game_on  = 0;
int  connected = 0;
char status_msg[64] = "Type server then ENTER";

struct mosquitto *mosq;

void on_message(struct mosquitto *m, void *u, const struct mosquitto_message *msg) {
    char text[64] = {0};
    strncpy(text, (char *)msg->payload, 63);
    if (strcmp(msg->topic, "ttt/game/state") == 0)
        for (int i = 0; i < 9; i++) board[i] = text[i];
    if (strcmp(msg->topic, "ttt/game/status") == 0) {
        if (strcmp(text, "your_turn") == 0)        { my_turn=1; strcpy(status_msg, "Your turn! Click a square."); }
        if (strcmp(text, "opponent_turn") == 0)    { my_turn=0; strcpy(status_msg, "Opponent's turn..."); }
        if (strcmp(text, "game_started_1p") == 0)  { game_on=1; strcpy(status_msg, "Game started!"); }
        if (strcmp(text, "game_started_2p") == 0)  { game_on=1; strcpy(status_msg, "Waiting for ESP32..."); }
        if (strcmp(text, "winner:X") == 0)         { game_on=0; strcpy(status_msg, "You won!"); }
        if (strcmp(text, "winner:O") == 0)         { game_on=0; strcpy(status_msg, "You lost!"); }
        if (strcmp(text, "draw") == 0)             { game_on=0; strcpy(status_msg, "Draw!"); }
    }
}

void on_connect(struct mosquitto *m, void *u, int rc) {
    mosquitto_subscribe(m, NULL, "ttt/game/state",  0);
    mosquitto_subscribe(m, NULL, "ttt/game/status", 0);
    connected = 1;
    strcpy(status_msg, "Connected! Press 1 or 2.");
}

int main() {
    mosquitto_lib_init();
    mosq = mosquitto_new("laptop", true, NULL);
    mosquitto_connect_callback_set(mosq, on_connect);
    mosquitto_message_callback_set(mosq, on_message);

    InitWindow(400, 500, "Tic Tac Toe");
    SetTargetFPS(30);

    while (!WindowShouldClose()) {

        // text input for server
        if (!connected) {
            int key = GetCharPressed();
            while (key > 0) {
                int len = strlen(MQTT_HOST);
                if (len < 127) { MQTT_HOST[len] = (char)key; MQTT_HOST[len+1] = '\0'; }
                key = GetCharPressed();
            }
            if (IsKeyPressed(KEY_BACKSPACE) && strlen(MQTT_HOST) > 0)
                MQTT_HOST[strlen(MQTT_HOST)-1] = '\0';
            if (IsKeyPressed(KEY_ENTER) && strlen(MQTT_HOST) > 0) {
                mosquitto_connect(mosq, MQTT_HOST, MQTT_PORT, 60);
                mosquitto_loop_start(mosq);
            }
        }

        // game input
        if (connected) {
            if (IsKeyPressed(KEY_ONE)) {
                for (int i = 0; i < 9; i++) board[i] = ' ';
                mosquitto_publish(mosq, NULL, "ttt/game/control", 8, "start_1p", 0, false);
                strcpy(status_msg, "1 Player starting...");
            }
            if (IsKeyPressed(KEY_TWO)) {
                for (int i = 0; i < 9; i++) board[i] = ' ';
                mosquitto_publish(mosq, NULL, "ttt/game/control", 8, "start_2p", 0, false);
                strcpy(status_msg, "2 Player - waiting for ESP32...");
            }
            if (game_on && my_turn && IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) {
                Vector2 mouse = GetMousePosition();
                int col = (int)mouse.x / 133;
                int row = ((int)mouse.y - 100) / 133;
                if (col >= 0 && col < 3 && row >= 0 && row < 3) {
                    int i = row * 3 + col;
                    if (board[i] != 'X' && board[i] != 'O') {
                        char move[8];
                        snprintf(move, sizeof(move), "X:%d", i+1);
                        mosquitto_publish(mosq, NULL, "ttt/player/move", strlen(move), move, 0, false);
                        my_turn = 0;
                        strcpy(status_msg, "Waiting for opponent...");
                    }
                }
            }
        }

        BeginDrawing();
        ClearBackground(BLACK);

        // server input box at top
        DrawText("Server:", 10, 10, 16, GRAY);
        DrawRectangleLines(80, 5, 310, 25, connected ? DARKGRAY : GRAY);
        DrawText(MQTT_HOST, 85, 10, 16, connected ? DARKGRAY : WHITE);

        // status
        DrawText(status_msg, 10, 40, 16, GRAY);

        // grid lines
        DrawLine(133, 100, 133, 500, GRAY);
        DrawLine(266, 100, 266, 500, GRAY);
        DrawLine(0,   233, 400, 233, GRAY);
        DrawLine(0,   366, 400, 366, GRAY);

        // X and O
        for (int i = 0; i < 9; i++) {
            int col = i % 3;
            int row = i / 3;
            int cx  = col * 133 + 66;
            int cy  = 100 + row * 133 + 66;
            if (board[i] == 'X') {
                DrawLine(cx-30, cy-30, cx+30, cy+30, BLUE);
                DrawLine(cx+30, cy-30, cx-30, cy+30, BLUE);
            } else if (board[i] == 'O') {
                DrawCircleLines(cx, cy, 30, RED);
            } else {
                char num[2] = {'1'+i, '\0'};
                DrawText(num, cx-5, cy-8, 18, DARKGRAY);
            }
        }

        EndDrawing();
    }

    mosquitto_loop_stop(mosq, true);
    mosquitto_destroy(mosq);
    mosquitto_lib_cleanup();
    CloseWindow();
    return 0;
}