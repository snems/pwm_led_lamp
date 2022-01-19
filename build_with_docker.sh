#!/bin/bash
#docker run -it --rm --net=host --env="DISPLAY" --volume="$PWD/../:/home/username:rw" snems/stm32_builder
docker run -it --rm --net=host --volume="$PWD:/home/username/build:rw" snems/stm32_builder bash /home/username/build/docker/build_script
