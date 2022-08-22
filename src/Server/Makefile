CC = g++
FLAG = -lpthread
SERVER = dataServer.cpp
SOBJ = dataServer.o
SEXEC = dataServer

$(SEXEC) : $(SOBJ)
	$(CC) -o $(SEXEC) $(SOBJ) $(FLAG)

$(SOBJ) : 
	$(CC) -c $(SERVER) $(FLAG)
