
CFLAGS=$(shell pkg-config --cflags libusb-1.0)
LDLIBS=$(shell pkg-config --libs libusb-1.0)

APP=pcan-id

$(APP): $(APP).o

clean:
	rm -rf $(APP) *.o
