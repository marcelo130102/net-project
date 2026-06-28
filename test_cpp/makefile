default: all

all: server client

client:
	g++ client.cpp protocol.cpp -o client.exe

server:
	g++ server.cpp protocol.cpp -o server.exe

clean:
	rm *.exe
