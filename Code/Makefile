APP = raster_clip_studio
SRC = main.cpp

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
CPPFLAGS += $(shell pkg-config --cflags glfw3 libpng 2>/dev/null)
LDLIBS += $(shell pkg-config --libs glfw3 libpng 2>/dev/null)

UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Darwin)
CPPFLAGS += -DGL_SILENCE_DEPRECATION
LDFLAGS += -framework OpenGL
else
LDLIBS += -lGL
endif

all: $(APP)

$(APP): $(SRC)
	$(CXX) $(CXXFLAGS) $(CPPFLAGS) -o $(APP) $(SRC) $(LDFLAGS) $(LDLIBS)

run: $(APP)
	./$(APP)
