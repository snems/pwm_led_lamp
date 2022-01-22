#!/bin/bash
. ./util/config
. ./util/funct.sh

docker_script_run script_generate_board
docker_script_run script_build
