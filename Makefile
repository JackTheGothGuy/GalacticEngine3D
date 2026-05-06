CXX      = g++
CXXFLAGS = -std=c++17 -O2 -Iinclude \
           $(shell pkg-config --cflags glfw3 glew assimp)
LDFLAGS  = -lGL -lGLEW -lglfw -lassimp \
           $(shell pkg-config --libs glfw3 glew assimp)

TARGET  = TwilightEngine
SRC     = engine/renderer.cpp

# stb_image is a single-header lib — download it if missing
INCLUDE_DIR = include
STB_HEADER  = $(INCLUDE_DIR)/stb_image.h
STB_URL     = https://raw.githubusercontent.com/nothings/stb/master/stb_image.h

.PHONY: all clean shaders

all: $(STB_HEADER) $(TARGET)

# Fetch stb_image if not already present
$(STB_HEADER):
	@mkdir -p $(INCLUDE_DIR)
	@echo "Fetching stb_image.h..."
	@curl -sSL $(STB_URL) -o $(STB_HEADER) || wget -q $(STB_URL) -O $(STB_HEADER)

$(TARGET): $(SRC)
	$(CXX) $(SRC) -o $(TARGET) $(CXXFLAGS) $(LDFLAGS)

# List all shader files for reference / quick validation
shaders:
	@echo "--- Vertex shaders ---"
	@ls shaders/*.vert
	@echo "--- Fragment shaders ---"
	@ls shaders/*.frag

clean:
	rm -f $(TARGET)
