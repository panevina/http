all: compile

compile:
	gcc -pthread -o main main.c

clean:
	rm main
