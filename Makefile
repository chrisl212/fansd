CC=gcc
CFLAGS= -g -Wall
TARGET=fansd
SERVICE=fans.service

all: fans.c
	$(CC) $(CFLAGS) -o $(TARGET) fans.c
install: $(TARGET)
	cp $(TARGET) /usr/bin/
	cp $(SERVICE) /etc/systemd/system/
	systemctl enable $(SERVICE)
clean:
	rm $(TARGET)
