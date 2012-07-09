#!/bin/sh

gcc -O3 -march=native -mtune=native server.c -lev -o server
gcc -O3 -march=native -mtune=native server-honest.c -lev -o server-honest

gcc -O3 -march=native -mtune=native client.c -lev -o client

gcc -O3 -march=native -mtune=native thread.c -lev -lpthread -o thread
gcc -O3 -march=native -mtune=native thread-honest.c -lev -lpthread -o thread-honest

