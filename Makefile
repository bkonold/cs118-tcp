all: server receiver

server: server.cpp Packet.h
	g++ -o server server.cpp -w

receiver: receiver.cpp Packet.h
	g++ -o receiver receiver.cpp -w

clean:
	rm -rf *.o server receiver