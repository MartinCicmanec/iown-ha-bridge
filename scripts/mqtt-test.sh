#!/usr/bin/env bash
# Exercise the MQTT firmware (env ttgo-lora32-v21-mqtt) without Home Assistant.
# Needs mosquitto_pub / mosquitto_sub (package "mosquitto" or "mosquitto-clients").
#
# Usage:
#   scripts/mqtt-test.sh watch                    # print everything under iown/# and homeassistant/#
#   scripts/mqtt-test.sh <blind> open|close|stop  # e.g. scripts/mqtt-test.sh Blind_1 open
#   scripts/mqtt-test.sh <blind> my               # go to favourite position
#   scripts/mqtt-test.sh <blind> counter <n>      # set the rolling counter (decimal)
#   scripts/mqtt-test.sh <blind> pair             # pair the blind (motor in pairing mode)
#
# Broker settings come from the environment:
#   MQTT_HOST (default localhost), MQTT_PORT (1883), MQTT_USER, MQTT_PASS
set -euo pipefail

HOST=${MQTT_HOST:-localhost}
PORT=${MQTT_PORT:-1883}
AUTH=()
[[ -n ${MQTT_USER:-} ]] && AUTH+=(-u "$MQTT_USER")
[[ -n ${MQTT_PASS:-} ]] && AUTH+=(-P "$MQTT_PASS")

pub() {
  echo "-> $1 = $2"
  mosquitto_pub -h "$HOST" -p "$PORT" "${AUTH[@]}" -t "$1" -m "$2"
}

usage() { sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 1; }

[[ $# -ge 1 ]] || usage
if [[ $1 == watch ]]; then
  exec mosquitto_sub -h "$HOST" -p "$PORT" "${AUTH[@]}" -v -t 'iown/#' -t 'homeassistant/#'
fi

[[ $# -ge 2 ]] || usage
blind=$1
case $2 in
  open)    pub "iown/$blind/set" OPEN ;;
  close)   pub "iown/$blind/set" CLOSE ;;
  stop)    pub "iown/$blind/set" STOP ;;
  my)      pub "iown/$blind/my" MY ;;
  counter) [[ $# -eq 3 ]] || usage; pub "iown/$blind/counter/set" "$3" ;;
  pair)    pub "iown/$blind/pair" PAIR ;;
  *)       usage ;;
esac
