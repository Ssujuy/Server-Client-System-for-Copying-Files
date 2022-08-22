SD = Server
CD = Client
SEXEC = 
MAKE = make
RV = rm -f
COBJ = remoteClient.o
CEXEC = remoteClient
SOBJ = dataServer.o
SEXEC = dataServer

all : 
	cd ./$(CD) && $(MAKE)
	cd ./$(SD) && $(MAKE)

clean :
	cd ./$(CD) && $(RV) $(COBJ)
	cd ./$(CD) && $(RV) $(CEXEC)
	cd ./$(SD) && $(RV) $(SOBJ)
	cd ./$(SD) && $(RV) $(SEXEC)






