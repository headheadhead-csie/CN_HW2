CC = gcc
CXX = g++
INCLUDE_OPENCV = `pkg-config --cflags --libs opencv`
LINK_PTHREAD = -lpthread

CLIENT = client.cpp
SERVER = server.cpp
OPEN_CV = openCV.cpp
PTHREAD = pthread.c
CLI = client
SER = server
CV = openCV
PTH = pthread

all: server client opencv
  
server: $(SERVER) hw2.h
	$(CXX) $(SERVER) -o $(SER) $(INCLUDE_OPENCV)
client: $(CLIENT) hw2.h
	$(CXX) $(CLIENT) -o $(CLI) $(INCLUDE_OPENCV)
opencv: $(OPEN_CV)
	$(CXX) $(OPEN_CV) -o $(CV) $(INCLUDE_OPENCV)

.PHONY: clean

clean:
	rm $(CLI) $(SER)
