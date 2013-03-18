#!/bin/sh
./server &
websockify --web=. 8080 localhost:12345
killall server
