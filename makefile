SOURCE  := $(wildcard *.c) $(wildcard *.cpp)
OBJS    := $(patsubst %.c,%.o,$(patsubst %.cpp,%.o,$(SOURCE)))

TARGET  := server

CC      := g++
LIBS    :=  -lpthread -lmysqlclient

.PHONY : everything objs clean rebuild

everything : $(TARGET)

all : $(TARGET)

objs : $(OBJS)

rebuild: clean everything

clean :
	rm -fr *.so
	rm -fr *.o
	rm -fr $(TARGET)

$(TARGET) : $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)
