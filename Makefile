CC = gcc
#CC = clang
CXX = g++
#CXX = clang++

INCLUDES = ./

CFLAGS = -O3 -fopenmp -Wall -Wno-parentheses
#CFLAGS += -g -Wall -Wno-parenthese
CFLAGS += -I$(INCLUDES)
#CFLAGS += -DDEBUG

CPPFLAGS = $(CFLAGS) -std=c++14
CPPFLAGS += `pkg-config opencv --cflags`

LDFLAGS += `pkg-config opencv --libs`
LDFLAGS += -lboost_system
#LDFLAGS += -lboost_filesystem
LDFLAGS += -lusb-1.0

CSRCS =
CPPSRCS = main.cpp spectrometer.cpp

OBJS = $(CSRCS:.c=.o) $(CPPSRCS:.cpp=.o)

TARGET = a.out

.PHONY: depend clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) -o $(TARGET) $(OBJS) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CPPFLAGS) -o $@ -c $<

clean:
	$(RM) $(OBJS) $(EXTRAS) $(TARGET)

distclean: clean
	$(RM) *~ .depend

depend: .depend

.depend: $(CSRCS) $(CPPSRCS)
	$(RM) ./.depend
	$(CXX) $(CPPFLAGS) -MM $^ >> ./.depend

include .depend
