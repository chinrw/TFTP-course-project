all: main

COMPILER = clang++

main: main.cpp
	$(COMPILER) main.cpp -o main
