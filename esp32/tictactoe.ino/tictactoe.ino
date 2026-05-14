#include <Keypad.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#include <WiFi.h>
#define MQTT_MAX_PACKET_SIZE 256
#include <PubSubClient.h>

const char* WIFI_SSID     = "Frontier8736";
const char* WIFI_PASSWORD = "6283033254";
const char* MQTT_SERVER   = "senen-cs2600.duckdns.org";
const int   MQTT_PORT     = 1883;

#define SDA 13
#define SCL 14
LiquidCrystal_I2C lcd(0x27, 16, 2); 

char keys[4][4] = {
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};
byte rowPins[4] = {27, 26, 25, 33};
byte colPins[4] = {15, 21, 22, 23};
Keypad myKeypad = Keypad(makeKeymap(keys), rowPins, colPins, 4, 4);

WiFiClient   espClient;
PubSubClient mqtt(espClient);

char board[9];
bool myTurn     = false;
bool gameActive = false;
int  scrollOffset = 0;

void lcdPrint(const char* row0, const char* row1 = "") {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print(String(row0).substring(0, 16));
  lcd.setCursor(0, 1);
  lcd.print(String(row1).substring(0, 16));
}

void displayBoard() {
  int start = scrollOffset * 6;
  char row0[17] = "";
  char row1[17] = "";
  char buf[6];
  for (int i = 0; i < 3 && (start+i) < 9; i++) {
    int idx = start+i;
    char cell = board[idx];
    if (cell != 'X' && cell != 'O') cell = '_';
    snprintf(buf, sizeof(buf), "%d:%c ", idx+1, cell);
    strcat(row0, buf);
  }
  for (int i = 3; i < 6 && (start+i) < 9; i++) {
    int idx = start+i;
    char cell = board[idx];
    if (cell != 'X' && cell != 'O') cell = '_';
    snprintf(buf, sizeof(buf), "%d:%c ", idx+1, cell);
    strcat(row1, buf);
  }
  lcd.clear();
  lcd.setCursor(0,0); lcd.print(row0);
  lcd.setCursor(0,1); lcd.print(row1);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  char msg[64] = {0};
  for (unsigned int i = 0; i < length && i < 63; i++)
    msg[i] = (char)payload[i];

  if (strcmp(topic, "ttt/game/state") == 0 && strlen(msg) == 9) {
    for (int i = 0; i < 9; i++) board[i] = msg[i];
    if (gameActive) displayBoard();
  }

  if (strcmp(topic, "ttt/game/status") == 0) {
    if (strcmp(msg, "game_started_2p") == 0) {
      gameActive = true; myTurn = false;
      for (int i = 0; i < 9; i++) board[i] = ' ';
      lcdPrint("Game started!", "Wait your turn");
    }
    else if (strcmp(msg, "your_turn") == 0) {
      myTurn = true;
      lcdPrint("YOUR TURN!", "# = scroll");
      delay(1000); displayBoard();
    }
    else if (strcmp(msg, "opponent_turn") == 0) {
      myTurn = false; lcdPrint("Opponent", "thinking...");
    }
    else if (strcmp(msg, "invalid_position_taken") == 0) {
      myTurn = true; lcdPrint("Taken! Try", "another spot");
      delay(1500); displayBoard();
    }
    else if (strcmp(msg, "winner:O") == 0) { gameActive=false; lcdPrint("You won! :)","GG!"); }
    else if (strcmp(msg, "winner:X") == 0) { gameActive=false; lcdPrint("You lost :(",""); }
    else if (strcmp(msg, "draw") == 0)     { gameActive=false; lcdPrint("Draw!",""); }
  }
}

void connectWiFi() {
  lcdPrint("Connecting WiFi","...");
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 30) { delay(500); attempts++; }
  if (WiFi.status() == WL_CONNECTED) lcdPrint("WiFi OK!","");
  else lcdPrint("WiFi FAILED","Check password");
  delay(1500);
}

void connectMQTT() {
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setKeepAlive(60);
  mqtt.setCallback(mqttCallback);
  lcdPrint("Connecting MQTT","...");
  while (!mqtt.connected()) {
    if (mqtt.connect("ESP32_TicTacToe")) {
      mqtt.subscribe("ttt/game/state");
      mqtt.subscribe("ttt/game/status");
      lcdPrint("MQTT OK!","Waiting for game");
    } else {
      lcdPrint("MQTT failed","Retrying...");
      delay(3000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(500);
  Wire.begin(SDA, SCL);
  lcd.init();
  lcd.backlight();
  lcdPrint("TicTacToe","Starting...");
  delay(1000);
  for (int i = 0; i < 9; i++) board[i] = ' ';
  connectWiFi();
  connectMQTT();
}

void loop() {
  if (!mqtt.connected()) connectMQTT();
  mqtt.loop();

  char key = myKeypad.getKey();
  if (!key) return;

  if (key == '#') { scrollOffset = 1-scrollOffset; if (gameActive) displayBoard(); return; }
  if (key == '*') { scrollOffset = 0; if (gameActive) displayBoard(); return; }

  if (key >= '1' && key <= '9') {
    if (!gameActive) { lcdPrint("No game yet","Wait for laptop"); return; }
    if (!myTurn)     { lcdPrint("Not your turn!",""); return; }
    int idx = (key-'0')-1;
    if (board[idx]=='X'||board[idx]=='O') {
      lcdPrint("Taken! Try","another spot");
      delay(1500); displayBoard(); return;
    }
    char moveMsg[8];
    snprintf(moveMsg, sizeof(moveMsg), "O:%c", key);
    mqtt.publish("ttt/player/move", moveMsg);
    myTurn = false;
    lcdPrint("Move sent!","Waiting...");
  }
}