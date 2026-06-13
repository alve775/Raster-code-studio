# Raster Clip Studio

Course: CSE 4201 Computer Graphics and Animations

This submission implements an interactive C++ OpenGL application for midpoint line rasterization and Cohen-Sutherland line clipping. It uses classroom-style immediate mode drawing with `glBegin`, `glVertex`, `glColor`, `glMatrixMode`, and `glOrtho`.

## Files

- `Code/main.cpp` - C++ OpenGL source code.
- `Code/Makefile` - build file for compiling, running, exporting screenshots, compiling the report, and creating the ZIP.
- `Report/Raster_Clip_Studio_Report.pdf` - report PDF.
- `Screenshots/` - five OpenGL-rendered test case screenshots and the coordinate log.

## Requirements

macOS with Homebrew:

```bash
brew install glfw libpng
```
## macos-Compatible Makefile
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



Ubuntu/Debian:

```bash
sudo apt-get install -y make g++ libglfw3-dev libpng-dev texlive-latex-base texlive-latex-extra
```

Windows with MSYS2 UCRT64:

```bash
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-glfw mingw-w64-ucrt-x86_64-libpng mingw-w64-ucrt-x86_64-pkgconf make zip
```

On Windows, use the **MSYS2 UCRT64** terminal, not normal Command Prompt.

## Build And Run

From the submission root:

```bash
cd Code
make
```

On macOS/Linux:

```bash
./raster_clip_studio
```

On Windows/MSYS2:

```bash
./raster_clip_studio.exe
```

You can also run:

```bash
make run
```

## Windows-Compatible Makefile

For Windows/MSYS2 support, the `Code/Makefile` should use `-lopengl32` and produce a `.exe` file on Windows:

```make
APP = raster_clip_studio
SRC = main.cpp

CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
CPPFLAGS += $(shell pkg-config --cflags glfw3 libpng 2>/dev/null)
LDLIBS += $(shell pkg-config --libs glfw3 libpng 2>/dev/null)

UNAME_S := $(shell uname -s)

ifeq ($(OS),Windows_NT)
APP = raster_clip_studio.exe
LDLIBS += -lopengl32
else ifeq ($(UNAME_S),Darwin)
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


clean:
	rm -f $(APP) *.o
	rm -f ../Report/*.aux ../Report/*.log ../Report/*.out ../Report/*.toc

.PHONY: all run screenshots report zip clean


## Controls

- `1` to `5` - load the required test cases.
- Left mouse click on the grid - first click selects `P1(x1,y1)`, second click selects `P2(x2,y2)`.
- Right mouse click - cancel the current mouse point selection.
- `Space` - animate midpoint raster pixels step by step.
- `E` - optional keyboard edit for the clipping window only. Type `xmin ymin xmax ymax`, then press `Enter`; use `Backspace` to edit and `Esc` to cancel.
- `S` - save the current OpenGL view as `manual_export.png`.
- `R` - reset view.
- `G` - toggle grid.
- `C` - toggle clipped result.
- Arrow keys - pan.
- `+` / `-` - zoom.
- `Esc` - close.


