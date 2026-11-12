#!/bin/bash
# Builds and runs the GameService simulator. Needs gcc (sudo apt install build-essential).
cd "$(dirname "$0")" || exit 1
ROOT=../..
gcc -std=gnu99 -w -Ifake -I$ROOT/FrameworkHeaders -I$ROOT/ProjectHeaders \
    sim_main.c $ROOT/ProjectSource/GameService.c -o /tmp/game_sim || exit 1
/tmp/game_sim
