CXX = g++
CXXFLAGS =  -O3 -march=native -std=c++17 
LDFLAGS = -lcurl -lstdc++fs

SRC = $(wildcard sources/*.cpp)
OBJ = $(SRC:.cpp=.o)
TARGET = coarseETA

all: $(TARGET)

$(TARGET): $(OBJ)
	$(CXX) $(OBJ) -o $(TARGET) $(LDFLAGS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@ $(LDFLAGS)

clean:
	rm -f $(OBJ) $(TARGET)