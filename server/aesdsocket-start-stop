#!/bin/sh

# Start/stop script for aesdsocket daemon
# Thomas Ames, ECEA 5305, Assignment 5 Part 2, July 2023

case "$1" in
    start)
	echo "Starting aesdsocket"
	start-stop-daemon -S -n aesdsocket -a /usr/bin/aesdsocket -- -d
	;;
    stop)
	echo "Stopping aesdsocket"
	start-stop-daemon -K -n aesdsocket
	;;
    *)
	echo "Usage: $0 {start|stop}"
	exit 1
esac

exit 0
