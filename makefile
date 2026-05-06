CXX = g++
CXXFLAGS = -std=c++17 -Iinclude $(shell pkg-config --cflags glfw3 glew assimp)
LDFLAGS = -lGL -lGLEW -lglfw -lassimp $(shell pkg-config --libs glfw3 glew assimp)

TARGET = TwilightEngine
SRC = engine/renderer.cpp

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(SRC) -o $(TARGET) $(CXXFLAGS) $(LDFLAGS)

clean:
	rm -f $(TARGET)

 