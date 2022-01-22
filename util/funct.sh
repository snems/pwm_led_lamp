
function docker_script_run 
{
	docker run -it --privileged -v /dev/bus/usb:/dev/bus/usb --rm --net=host \
	    --volume="$PWD:$DOCKER_SOURCE_PATH:rw" $DOCKER_IMAGE bash $DOCKER_UTIL_PATH/$1
}

