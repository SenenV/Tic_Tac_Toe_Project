#!/bin/bash


MQTT_HOST="localhost"
MQTT_PORT="1883"

# board array, 9 spots
declare -a BOARD=(' ' ' ' ' ' ' ' ' ' ' ' ' ' ' ' ' ')
MY_TURN=false
GAME_ACTIVE=false

# send a move to the server
send_move() {
  local pos=$1
  mosquitto_pub -h "$MQTT_HOST" -p "$MQTT_PORT" -t "ttt/player/move" -m "O:$pos"
}

# update copy of the board
update_board() {
  local state="$1"
  for i in {0..8}; do
    BOARD[$i]="${state:$i:1}"
  done
}

# gets list of empty positions
get_available() {
  local available=()
  for i in {0..8}; do
    if [ "${BOARD[$i]}" = " " ]; then
      available+=($((i + 1)))
    fi
  done
  echo "${available[@]}"
}

# picks a position to play
pick_move() {
  local available
  read -r -a available <<< "$(get_available)"

  # just picks a random available spot
  local rand_idx=$(( RANDOM % ${#available[@]} ))
  echo "${available[$rand_idx]}"
}

# handles incoming mqtt messages
handle_message() {
  local topic="$1"
  local msg="$2"

  # board update
  if [ "$topic" = "ttt/game/state" ] && [ ${#msg} -eq 9 ]; then
    update_board "$msg"
  fi

  # status update
  if [ "$topic" = "ttt/game/status" ]; then
    if [ "$msg" = "game_started_1p" ]; then
      GAME_ACTIVE=true
      MY_TURN=false
      BOARD=(' ' ' ' ' ' ' ' ' ' ' ' ' ' ' ' ' ')
    fi
    if [ "$msg" = "your_turn" ]; then
      MY_TURN=true
      sleep 1
      local move
      move=$(pick_move)
      send_move "$move"
      MY_TURN=false
    fi
    if [ "$msg" = "winner:O" ] || [ "$msg" = "winner:X" ] || [ "$msg" = "draw" ]; then
      GAME_ACTIVE=false
      MY_TURN=false
    fi
  fi
}

echo "Bash player started. Waiting for game..."

# listen for messages
mosquitto_sub -h "$MQTT_HOST" -p "$MQTT_PORT" \
  -t "ttt/game/state" \
  -t "ttt/game/status" \
  -v | while IFS= read -r line; do
    topic="${line%% *}"
    msg="${line#* }"
    handle_message "$topic" "$msg"
  done
