CC = g++
CFLAGS = -std=c++20 -Wall -O2 -I./include -I./deps/jerasure/include -I./deps/gf-complete/install/include
LDFLAGS = -L./deps/jerasure/install/lib -L./deps/gf-complete/install/lib -ljerasure -lgf_complete
OBJECTS = src/ErasureCode.o src/ErasureCodeJerasure.o src/ErasureCodeClay.o src/main.o
EXECUTABLE = clay_ec

all: $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

src/ErasureCode.o: src/ErasureCode.cc include/ErasureCode.h include/BufferList.h
	$(CC) $(CFLAGS) -c src/ErasureCode.cc -o src/ErasureCode.o

src/ErasureCodeJerasure.o: src/ErasureCodeJerasure.cc include/ErasureCodeJerasure.h include/ErasureCode.h include/BufferList.h
	$(CC) $(CFLAGS) -c src/ErasureCodeJerasure.cc -o src/ErasureCodeJerasure.o

src/ErasureCodeClay.o: src/ErasureCodeClay.cc include/ErasureCodeClay.h include/ErasureCode.h include/BufferList.h 
	$(CC) $(CFLAGS) -c src/ErasureCodeClay.cc -o src/ErasureCodeClay.o

src/main.o: src/main.cpp include/ErasureCodeClay.h include/ErasureCode.h include/BufferList.h
	$(CC) $(CFLAGS) -c src/main.cpp -o src/main.o

src/main.o: src/main.cpp
	$(CC) $(CFLAGS) -c src/main.cpp -o src/main.o

clean:
	rm -f $(OBJECTS) $(EXECUTABLE)

.PHONY: all clean
