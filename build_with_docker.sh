#!/bin/bash

function docker_script_run 
{
	docker run -it --rm --net=host --volume="$PWD:/home/username/build:rw" snems/stm32_builder bash $1
}

docker_script_run /home/username/build/docker/script_generate_board
docker_script_run /home/username/build/docker/script_build
