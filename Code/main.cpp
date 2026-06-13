#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#ifndef GLFW_VERSION_MAJOR
#error                                                                         \
    "GLFW header was not loaded. Install GLFW and compile with the Makefile in this Code folder."
#endif

#ifdef __APPLE__
#include <OpenGL/gl.h>
#else
#include <GL/gl.h>
#endif
#include <png.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using namespace std;

namespace fs = filesystem;

struct Color {
  float r;
  float g;
  float b;
};

struct Point {
  double x;
  double y;
};

struct WindowRect {
  double xmin;
  double ymin;
  double xmax;
  double ymax;
};

struct PixelStep {
  int x;
  int y;
  int decision;
};

struct TestCase {
  string name;
  Point p1;
  Point p2;
  WindowRect clip;
  string note;
};

struct ClipResult {
  bool accepted = false;
  Point p1;
  Point p2;
  int originalOut1 = 0;
  int originalOut2 = 0;
  int finalOut1 = 0;
  int finalOut2 = 0;
  vector<string> iterations;
};

struct Bounds {
  double xmin;
  double ymin;
  double xmax;
  double ymax;
};

struct Viewport {
  int x;
  int y;
  int width;
  int height;
};

struct Vec2 {
  float x;
  float y;
};

const int OUT_LEFT = 1;
const int OUT_RIGHT = 2;
const int OUT_BOTTOM = 4;
const int OUT_TOP = 8;

const Color BACKGROUND{0.07f, 0.08f, 0.10f};
const Color GRID{0.22f, 0.25f, 0.28f};
const Color AXIS{0.48f, 0.54f, 0.58f};
const Color ORIGINAL_LINE{0.58f, 0.62f, 0.68f};
const Color PIXEL_FILL{0.95f, 0.58f, 0.16f};
const Color PIXEL_EDGE{0.25f, 0.16f, 0.08f};
const Color CLIP_WINDOW{0.23f, 0.56f, 0.95f};
const Color CLIPPED_LINE{0.22f, 0.83f, 0.46f};
const Color REJECTED{0.92f, 0.25f, 0.25f};
const Color TEXT{0.90f, 0.93f, 0.96f};
const Color MUTED_TEXT{0.62f, 0.68f, 0.74f};
const Color PANEL{0.10f, 0.12f, 0.16f};

const vector<TestCase> TEST_CASES = {
    {"Case 1 - line completely inside",
     {2, 2},
     {8, 6},
     {0, 0, 10, 8},
     "Accepted without changing endpoints."},
    {"Case 2 - line completely outside",
     {-8, 8},
     {-3, 12},
     {0, 0, 10, 8},
     "Rejected because both endpoints share the LEFT outside region."},
    {"Case 3 - crossing one boundary",
     {-4, 4},
     {7, 4},
     {0, 0, 10, 8},
     "Left part is clipped at x = 0."},
    {"Case 4 - steep negative slope",
     {2, 9},
     {6, -3},
     {0, 0, 10, 8},
     "All-octant midpoint stepping keeps the raster pixels continuous."},
    {"Case 5 - vertical line",
     {5, -3},
     {5, 11},
     {0, 0, 10, 8},
     "Vertical clipping is handled without division-by-zero in rejection "
     "logic."}};

void setColor(Color color) { glColor3f(color.r, color.g, color.b); }

void drawLine(double x1, double y1, double x2, double y2, Color color) {
  setColor(color);
  glBegin(GL_LINES);
  glVertex2d(x1, y1);
  glVertex2d(x2, y2);
  glEnd();
}

void drawQuad(double x1, double y1, double x2, double y2, Color color) {
  setColor(color);
  glBegin(GL_QUADS);
  glVertex2d(x1, y1);
  glVertex2d(x2, y1);
  glVertex2d(x2, y2);
  glVertex2d(x1, y2);
  glEnd();
}

void drawRectangleLines(double x1, double y1, double x2, double y2,
                        Color color) {
  drawLine(x1, y1, x2, y1, color);
  drawLine(x2, y1, x2, y2, color);
  drawLine(x2, y2, x1, y2, color);
  drawLine(x1, y2, x1, y1, color);
}

string formatNumber(double value) {
  ostringstream out;
  if (abs(value - round(value)) < 1e-6) {
    out << static_cast<int>(round(value));
  } else {
    out << fixed << setprecision(2) << value;
  }
  return out.str();
}

string formatPoint(const Point &p) {
  return "(" + formatNumber(p.x) + "," + formatNumber(p.y) + ")";
}

string outcodeName(int code) {
  if (code == 0) {
    return "INSIDE";
  }
  string out;
  if (code & OUT_LEFT) {
    out += "LEFT|";
  }
  if (code & OUT_RIGHT) {
    out += "RIGHT|";
  }
  if (code & OUT_BOTTOM) {
    out += "BOTTOM|";
  }
  if (code & OUT_TOP) {
    out += "TOP|";
  }
  if (!out.empty()) {
    out.pop_back();
  }
  return out;
}

WindowRect normalizedWindow(WindowRect w) {
  if (w.xmin > w.xmax) {
    swap(w.xmin, w.xmax);
  }
  if (w.ymin > w.ymax) {
    swap(w.ymin, w.ymax);
  }
  return w;
}

int computeOutcode(double x, double y, WindowRect window) {
  window = normalizedWindow(window);
  int code = 0;
  if (x < window.xmin) {
    code |= OUT_LEFT;
  } else if (x > window.xmax) {
    code |= OUT_RIGHT;
  }
  if (y < window.ymin) {
    code |= OUT_BOTTOM;
  } else if (y > window.ymax) {
    code |= OUT_TOP;
  }
  return code;
}

vector<PixelStep> midpointLine(int x1, int y1, int x2, int y2) {
  vector<PixelStep> pixels;
  int dx = abs(x2 - x1);
  int dy = abs(y2 - y1);
  int sx = (x1 < x2) ? 1 : -1;
  int sy = (y1 < y2) ? 1 : -1;
  int x = x1;
  int y = y1;

  if (dx >= dy) {
    int decision = 2 * dy - dx;
    while (true) {
      pixels.push_back({x, y, decision});
      if (x == x2) {
        break;
      }
      x += sx;
      if (decision >= 0) {
        y += sy;
        decision += 2 * (dy - dx);
      } else {
        decision += 2 * dy;
      }
    }
  } else {
    int decision = 2 * dx - dy;
    while (true) {
      pixels.push_back({x, y, decision});
      if (y == y2) {
        break;
      }
      y += sy;
      if (decision >= 0) {
        x += sx;
        decision += 2 * (dx - dy);
      } else {
        decision += 2 * dx;
      }
    }
  }
  return pixels;
}

ClipResult cohenSutherland(Point start, Point end, WindowRect window) {
  window = normalizedWindow(window);
  ClipResult result;
  result.p1 = start;
  result.p2 = end;
  int out1 = computeOutcode(result.p1.x, result.p1.y, window);
  int out2 = computeOutcode(result.p2.x, result.p2.y, window);
  result.originalOut1 = out1;
  result.originalOut2 = out2;

  int guard = 0;
  while (guard++ < 12) {
    ostringstream row;
    row << "P1 " << outcodeName(out1) << " P2 " << outcodeName(out2);
    result.iterations.push_back(row.str());

    if ((out1 | out2) == 0) {
      result.accepted = true;
      break;
    }
    if ((out1 & out2) != 0) {
      result.accepted = false;
      break;
    }

    int out = out1 != 0 ? out1 : out2;
    double x = 0.0;
    double y = 0.0;

    if (out & OUT_TOP) {
      x = result.p1.x + (result.p2.x - result.p1.x) *
                            (window.ymax - result.p1.y) /
                            (result.p2.y - result.p1.y);
      y = window.ymax;
    } else if (out & OUT_BOTTOM) {
      x = result.p1.x + (result.p2.x - result.p1.x) *
                            (window.ymin - result.p1.y) /
                            (result.p2.y - result.p1.y);
      y = window.ymin;
    } else if (out & OUT_RIGHT) {
      y = result.p1.y + (result.p2.y - result.p1.y) *
                            (window.xmax - result.p1.x) /
                            (result.p2.x - result.p1.x);
      x = window.xmax;
    } else if (out & OUT_LEFT) {
      y = result.p1.y + (result.p2.y - result.p1.y) *
                            (window.xmin - result.p1.x) /
                            (result.p2.x - result.p1.x);
      x = window.xmin;
    }

    if (out == out1) {
      result.p1 = {x, y};
      out1 = computeOutcode(result.p1.x, result.p1.y, window);
    } else {
      result.p2 = {x, y};
      out2 = computeOutcode(result.p2.x, result.p2.y, window);
    }
  }

  result.finalOut1 = out1;
  result.finalOut2 = out2;
  return result;
}

array<unsigned char, 7> glyphRows(char ch) {
  ch = static_cast<char>(toupper(static_cast<unsigned char>(ch)));
  switch (ch) {
  case 'A':
    return {14, 17, 17, 31, 17, 17, 17};
  case 'B':
    return {30, 17, 17, 30, 17, 17, 30};
  case 'C':
    return {14, 17, 16, 16, 16, 17, 14};
  case 'D':
    return {30, 17, 17, 17, 17, 17, 30};
  case 'E':
    return {31, 16, 16, 30, 16, 16, 31};
  case 'F':
    return {31, 16, 16, 30, 16, 16, 16};
  case 'G':
    return {14, 17, 16, 23, 17, 17, 14};
  case 'H':
    return {17, 17, 17, 31, 17, 17, 17};
  case 'I':
    return {14, 4, 4, 4, 4, 4, 14};
  case 'J':
    return {7, 2, 2, 2, 18, 18, 12};
  case 'K':
    return {17, 18, 20, 24, 20, 18, 17};
  case 'L':
    return {16, 16, 16, 16, 16, 16, 31};
  case 'M':
    return {17, 27, 21, 21, 17, 17, 17};
  case 'N':
    return {17, 25, 21, 19, 17, 17, 17};
  case 'O':
    return {14, 17, 17, 17, 17, 17, 14};
  case 'P':
    return {30, 17, 17, 30, 16, 16, 16};
  case 'Q':
    return {14, 17, 17, 17, 21, 18, 13};
  case 'R':
    return {30, 17, 17, 30, 20, 18, 17};
  case 'S':
    return {15, 16, 16, 14, 1, 1, 30};
  case 'T':
    return {31, 4, 4, 4, 4, 4, 4};
  case 'U':
    return {17, 17, 17, 17, 17, 17, 14};
  case 'V':
    return {17, 17, 17, 17, 17, 10, 4};
  case 'W':
    return {17, 17, 17, 21, 21, 21, 10};
  case 'X':
    return {17, 17, 10, 4, 10, 17, 17};
  case 'Y':
    return {17, 17, 10, 4, 4, 4, 4};
  case 'Z':
    return {31, 1, 2, 4, 8, 16, 31};
  case '0':
    return {14, 17, 19, 21, 25, 17, 14};
  case '1':
    return {4, 12, 4, 4, 4, 4, 14};
  case '2':
    return {14, 17, 1, 2, 4, 8, 31};
  case '3':
    return {30, 1, 1, 14, 1, 1, 30};
  case '4':
    return {2, 6, 10, 18, 31, 2, 2};
  case '5':
    return {31, 16, 16, 30, 1, 1, 30};
  case '6':
    return {14, 16, 16, 30, 17, 17, 14};
  case '7':
    return {31, 1, 2, 4, 8, 8, 8};
  case '8':
    return {14, 17, 17, 14, 17, 17, 14};
  case '9':
    return {14, 17, 17, 15, 1, 1, 14};
  case '-':
    return {0, 0, 0, 31, 0, 0, 0};
  case '+':
    return {0, 4, 4, 31, 4, 4, 0};
  case '=':
    return {0, 0, 31, 0, 31, 0, 0};
  case '(':
    return {2, 4, 8, 8, 8, 4, 2};
  case ')':
    return {8, 4, 2, 2, 2, 4, 8};
  case '[':
    return {14, 8, 8, 8, 8, 8, 14};
  case ']':
    return {14, 2, 2, 2, 2, 2, 14};
  case '|':
    return {4, 4, 4, 4, 4, 4, 4};
  case ',':
    return {0, 0, 0, 0, 0, 4, 8};
  case '.':
    return {0, 0, 0, 0, 0, 12, 12};
  case ':':
    return {0, 12, 12, 0, 12, 12, 0};
  case '/':
    return {1, 1, 2, 4, 8, 16, 16};
  default:
    return {0, 0, 0, 0, 0, 0, 0};
  }
}

void drawText(double x, double topY, double scale, const string &text,
              Color color) {
  double cursor = x;
  setColor(color);
  for (char raw : text) {
    if (raw == ' ') {
      cursor += scale * 6.0;
      continue;
    }
    auto rows = glyphRows(raw);
    for (int row = 0; row < 7; ++row) {
      for (int col = 0; col < 5; ++col) {
        if (rows[row] & (1 << (4 - col))) {
          double x1 = cursor + col * scale;
          double y1 = topY - (row + 1) * scale;
          glBegin(GL_QUADS);
          glVertex2d(x1, y1);
          glVertex2d(x1 + scale * 0.9, y1);
          glVertex2d(x1 + scale * 0.9, y1 + scale * 0.9);
          glVertex2d(x1, y1 + scale * 0.9);
          glEnd();
        }
      }
    }
    cursor += scale * 6.0;
  }
}

Bounds caseBounds(const TestCase &testCase) {
  WindowRect w = normalizedWindow(testCase.clip);
  double xmin = min({testCase.p1.x, testCase.p2.x, w.xmin, w.xmax});
  double xmax = max({testCase.p1.x, testCase.p2.x, w.xmin, w.xmax});
  double ymin = min({testCase.p1.y, testCase.p2.y, w.ymin, w.ymax});
  double ymax = max({testCase.p1.y, testCase.p2.y, w.ymin, w.ymax});
  double margin = 2.0;
  return {floor(xmin) - margin, floor(ymin) - margin,
          ceil(xmax) + margin, ceil(ymax) + margin};
}

int sidePanelWidthFor(int framebufferWidth) {
  int safeWidth = max(1, framebufferWidth);
  int preferred = min(360, max(300, safeWidth / 3));
  return min(preferred, max(0, safeWidth - 1));
}

Viewport sceneViewportFor(int framebufferWidth, int framebufferHeight) {
  int safeWidth = max(1, framebufferWidth);
  int safeHeight = max(1, framebufferHeight);
  int panelWidth = sidePanelWidthFor(safeWidth);
  return {0, 0, max(1, safeWidth - panelWidth), safeHeight};
}

Bounds visibleWorldBounds(const TestCase &testCase, Viewport viewport,
                          float zoom, Vec2 pan) {
  Bounds b = caseBounds(testCase);
  double centerX = (b.xmin + b.xmax) * 0.5 + pan.x;
  double centerY = (b.ymin + b.ymax) * 0.5 + pan.y;
  double spanX = max(8.0, b.xmax - b.xmin);
  double spanY = max(8.0, b.ymax - b.ymin);
  double viewAspect =
      static_cast<double>(viewport.width) / static_cast<double>(viewport.height);
  double worldAspect = spanX / spanY;
  if (worldAspect < viewAspect) {
    spanX = spanY * viewAspect;
  } else {
    spanY = spanX / viewAspect;
  }
  spanX /= max(0.25f, zoom);
  spanY /= max(0.25f, zoom);

  return {centerX - spanX * 0.5, centerY - spanY * 0.5,
          centerX + spanX * 0.5, centerY + spanY * 0.5};
}

void setWorldProjection(const TestCase &testCase, Viewport viewport,
                        float zoom, Vec2 pan) {
  Bounds view = visibleWorldBounds(testCase, viewport, zoom, pan);

  glViewport(viewport.x, viewport.y, viewport.width, viewport.height);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(view.xmin, view.xmax, view.ymin, view.ymax, -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
}

void setScreenProjection(int width, int height) {
  glViewport(0, 0, width, height);
  glMatrixMode(GL_PROJECTION);
  glLoadIdentity();
  glOrtho(0.0, width, 0.0, height, -1.0, 1.0);
  glMatrixMode(GL_MODELVIEW);
  glLoadIdentity();
}

string pixelSequence(const vector<PixelStep> &pixels,
                          size_t limit = 1000) {
  ostringstream out;
  size_t n = min(limit, pixels.size());
  for (size_t i = 0; i < n; ++i) {
    if (i > 0) {
      out << " ";
    }
    out << "(" << pixels[i].x << "," << pixels[i].y << ")";
  }
  if (pixels.size() > n) {
    out << " ...";
  }
  return out.str();
}

void printCaseAnalysis(const TestCase &testCase) {
  auto pixels = midpointLine(static_cast<int>(round(testCase.p1.x)),
                             static_cast<int>(round(testCase.p1.y)),
                             static_cast<int>(round(testCase.p2.x)),
                             static_cast<int>(round(testCase.p2.y)));
  auto clip = cohenSutherland(testCase.p1, testCase.p2, testCase.clip);
  cout << "\n" << testCase.name << "\n";
  cout << "Line: " << formatPoint(testCase.p1) << " to "
            << formatPoint(testCase.p2) << "\n";
  cout << "Outcodes: P1=" << outcodeName(clip.originalOut1)
            << " P2=" << outcodeName(clip.originalOut2) << "\n";
  if (clip.accepted) {
    cout << "Clipped: " << formatPoint(clip.p1) << " to "
              << formatPoint(clip.p2) << "\n";
  } else {
    cout << "Clipped: rejected\n";
  }
  cout << "Midpoint pixels: " << pixelSequence(pixels, 30) << "\n";
}

class App {
public:
  App(GLFWwindow *window, int width, int height)
      : window_(window), width_(width), height_(height) {
    printCaseAnalysis(currentCase());
  }

  void setSize(int width, int height) {
    width_ = max(1, width);
    height_ = max(1, height);
  }

  const TestCase &currentCase() const {
    if (customMode_) {
      return customCase_;
    }
    return TEST_CASES[currentCaseIndex_];
  }

  void onKey(int key, int action) {
    if (action != GLFW_PRESS && action != GLFW_REPEAT) {
      return;
    }

    if (editMode_) {
      if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        editMode_ = false;
        editMessage_ = "EDIT CANCELLED";
      } else if ((key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) &&
                 action == GLFW_PRESS) {
        applyWindowEditBuffer();
      } else if ((key == GLFW_KEY_BACKSPACE || key == GLFW_KEY_DELETE) &&
                 !editBuffer_.empty()) {
        editBuffer_.pop_back();
      }
      return;
    }

    if (key >= GLFW_KEY_1 && key <= GLFW_KEY_5 && action == GLFW_PRESS) {
      currentCaseIndex_ = key - GLFW_KEY_1;
      customMode_ = false;
      mouseHasFirstPoint_ = false;
      resetView();
      printCaseAnalysis(currentCase());
      return;
    }
    switch (key) {
    case GLFW_KEY_ESCAPE:
      glfwSetWindowShouldClose(window_, GLFW_TRUE);
      break;
    case GLFW_KEY_SPACE:
      if (action == GLFW_PRESS) {
        animate_ = !animate_;
        if (animate_) {
          animationStep_ = 1;
          lastStepTime_ = glfwGetTime();
        }
      }
      break;
    case GLFW_KEY_RIGHT:
      pan_.x += 0.35f / zoom_;
      break;
    case GLFW_KEY_LEFT:
      pan_.x -= 0.35f / zoom_;
      break;
    case GLFW_KEY_UP:
      pan_.y += 0.35f / zoom_;
      break;
    case GLFW_KEY_DOWN:
      pan_.y -= 0.35f / zoom_;
      break;
    case GLFW_KEY_EQUAL:
    case GLFW_KEY_KP_ADD:
      zoom_ = min(4.0f, zoom_ * 1.12f);
      break;
    case GLFW_KEY_MINUS:
    case GLFW_KEY_KP_SUBTRACT:
      zoom_ = max(0.35f, zoom_ / 1.12f);
      break;
    case GLFW_KEY_G:
      if (action == GLFW_PRESS) {
        showGrid_ = !showGrid_;
      }
      break;
    case GLFW_KEY_C:
      if (action == GLFW_PRESS) {
        showClipped_ = !showClipped_;
      }
      break;
    case GLFW_KEY_R:
      if (action == GLFW_PRESS) {
        resetView();
      }
      break;
    case GLFW_KEY_S:
      if (action == GLFW_PRESS) {
        saveScreenshot("manual_export.png");
        cout << "Saved manual_export.png\n";
      }
      break;
    case GLFW_KEY_E:
      if (action == GLFW_PRESS) {
        promptWindowEdit();
      }
      break;
    default:
      break;
    }
  }

  void onChar(unsigned int codepoint) {
    if (!editMode_ || codepoint < 32 || codepoint > 126) {
      return;
    }
    char ch = static_cast<char>(codepoint);
    if (isdigit(static_cast<unsigned char>(ch)) || ch == '-' ||
        ch == '+' || ch == '.' || ch == ' ' || ch == ',' || ch == ';') {
      if (ch == ',' || ch == ';') {
        ch = ' ';
      }
      if (editBuffer_.size() < 80) {
        editBuffer_ += ch;
      }
    }
  }

  void onMouseButton(int button, int action, int mods) {
    (void)mods;
    if (action != GLFW_PRESS) {
      return;
    }

    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
      mouseHasFirstPoint_ = false;
      editMessage_ = "MOUSE PICK CANCELLED";
      return;
    }

    if (button != GLFW_MOUSE_BUTTON_LEFT || editMode_) {
      return;
    }

    int winW = 0;
    int winH = 0;
    int fbW = 0;
    int fbH = 0;
    glfwGetWindowSize(window_, &winW, &winH);
    glfwGetFramebufferSize(window_, &fbW, &fbH);
    if (winW <= 0 || winH <= 0 || fbW <= 0 || fbH <= 0) {
      return;
    }

    setSize(fbW, fbH);
    Viewport viewport = sceneViewportFor(fbW, fbH);

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(window_, &mouseX, &mouseY);

    double mouseFbX = mouseX * static_cast<double>(fbW) /
                      static_cast<double>(winW);
    double mouseFbY = mouseY * static_cast<double>(fbH) /
                      static_cast<double>(winH);
    double localX = mouseFbX - viewport.x;
    double localY = mouseFbY - viewport.y;

    if (localX < 0.0 || localX > viewport.width || localY < 0.0 ||
        localY > viewport.height) {
      editMessage_ = "CLICK INSIDE GRID";
      return;
    }

    Bounds world = visibleWorldBounds(currentCase(), viewport, zoom_, pan_);
    double u = localX / static_cast<double>(viewport.width);
    double v = 1.0 - (localY / static_cast<double>(viewport.height));
    double worldX = world.xmin + u * (world.xmax - world.xmin);
    double worldY = world.ymin + v * (world.ymax - world.ymin);
    int gridX = static_cast<int>(round(worldX));
    int gridY = static_cast<int>(round(worldY));
    Point picked{static_cast<double>(gridX), static_cast<double>(gridY)};

    cout << "Mouse window: (" << mouseX << ", " << mouseY << ")\n";
    cout << "Mouse framebuffer: (" << mouseFbX << ", " << mouseFbY
              << ")\n";
    cout << "World: (" << worldX << ", " << worldY << ")\n";
    cout << "Snapped grid: (" << gridX << ", " << gridY << ")\n";

    animate_ = false;
    animationStep_ = 1000;

    if (!mouseHasFirstPoint_) {
      mouseFirstPoint_ = picked;
      mouseHasFirstPoint_ = true;
      editMessage_ = "P1 SET CLICK P2";
      cout << "Mouse P1 selected at " << formatPoint(picked) << "\n";
      return;
    }

    TestCase c = currentCase();
    c.name = "Mouse-selected case";
    c.p1 = mouseFirstPoint_;
    c.p2 = picked;
    c.note = "Endpoints selected by mouse clicks on the OpenGL grid.";

    customCase_ = c;
    customMode_ = true;
    mouseHasFirstPoint_ = false;
    editMessage_ = "P2 SET MOUSE LINE READY";
    cout << "Mouse P2 selected at " << formatPoint(picked) << "\n";
    printCaseAnalysis(currentCase());
  }

  void update() {
    if (!animate_) {
      return;
    }
    auto pixels = currentPixels();
    double now = glfwGetTime();
    if (now - lastStepTime_ > 0.09) {
      if (animationStep_ < static_cast<int>(pixels.size())) {
        ++animationStep_;
      }
      lastStepTime_ = now;
    }
  }

  void render() {
    glClearColor(BACKGROUND.r, BACKGROUND.g, BACKGROUND.b, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_DEPTH_TEST);

    Viewport sceneViewport = sceneViewportFor(width_, height_);
    int panelWidth = width_ - sceneViewport.width;
    renderWorld(sceneViewport);
    renderOverlay(panelWidth);
  }

  void saveScreenshot(const fs::path &outputPath) {
    render();
    glFinish();
    vector<unsigned char> pixels(width_ * height_ * 4);
    glReadPixels(0, 0, width_, height_, GL_RGBA, GL_UNSIGNED_BYTE,
                 pixels.data());
    writePng(outputPath, pixels, width_, height_);
  }

private:
  GLFWwindow *window_ = nullptr;
  int width_ = 1280;
  int height_ = 820;
  int currentCaseIndex_ = 0;
  TestCase customCase_ = TEST_CASES[0];
  bool customMode_ = false;
  bool showGrid_ = true;
  bool showClipped_ = true;
  bool animate_ = false;
  bool editMode_ = false;
  bool mouseHasFirstPoint_ = false;
  Point mouseFirstPoint_{0.0, 0.0};
  string editBuffer_;
  string editMessage_;
  int animationStep_ = 1000;
  double lastStepTime_ = 0.0;
  float zoom_ = 1.0f;
  Vec2 pan_{0.0f, 0.0f};

  void resetView() {
    zoom_ = 1.0f;
    pan_ = {0.0f, 0.0f};
    animate_ = false;
    animationStep_ = 1000;
  }

  vector<PixelStep> currentPixels() const {
    const auto &c = currentCase();
    return midpointLine(static_cast<int>(round(c.p1.x)),
                        static_cast<int>(round(c.p1.y)),
                        static_cast<int>(round(c.p2.x)),
                        static_cast<int>(round(c.p2.y)));
  }

  string panelCaseTitle() const {
    if (customMode_) {
      return "CUSTOM CASE";
    }
    switch (currentCaseIndex_) {
    case 0:
      return "CASE 1 INSIDE";
    case 1:
      return "CASE 2 OUTSIDE";
    case 2:
      return "CASE 3 ONE BOUNDARY";
    case 3:
      return "CASE 4 STEEP NEGATIVE";
    case 4:
      return "CASE 5 VERTICAL";
    default:
      return "CASE";
    }
  }

  void promptWindowEdit() {
    editMode_ = true;
    mouseHasFirstPoint_ = false;
    editBuffer_.clear();
    editMessage_ = "TYPE WINDOW NUMBERS";
    animate_ = false;
    cout << "\nEdit mode: type xmin ymin xmax ymax in the OpenGL window, then press Enter.\n";
  }

  void applyWindowEditBuffer() {
    istringstream input(editBuffer_);
    double values[4];
    for (double &value : values) {
      if (!(input >> value)) {
        editMessage_ = "NEED 4 NUMBERS";
        return;
      }
    }

    TestCase c = currentCase();
    c.name = "Custom window case";
    c.clip = {values[0], values[1], values[2], values[3]};
    c.note = "Clipping window entered directly in the OpenGL window.";

    customCase_ = c;
    customMode_ = true;
    editMode_ = false;
    mouseHasFirstPoint_ = false;
    editMessage_ = "WINDOW APPLIED";
    resetView();
    printCaseAnalysis(currentCase());
  }

  void renderWorld(Viewport sceneViewport) {
    const TestCase &c = currentCase();
    WindowRect w = normalizedWindow(c.clip);
    setWorldProjection(c, sceneViewport, zoom_, pan_);

    Bounds b = caseBounds(c);
    int minX = static_cast<int>(floor(b.xmin));
    int maxX = static_cast<int>(ceil(b.xmax));
    int minY = static_cast<int>(floor(b.ymin));
    int maxY = static_cast<int>(ceil(b.ymax));

    if (showGrid_) {
      for (int x = minX; x <= maxX; ++x) {
        drawLine(x, minY, x, maxY, GRID);
      }
      for (int y = minY; y <= maxY; ++y) {
        drawLine(minX, y, maxX, y, GRID);
      }
      drawLine(minX, 0, maxX, 0, AXIS);
      drawLine(0, minY, 0, maxY, AXIS);
    } 

    auto pixels = currentPixels();
    int pixelLimit =
        animate_ ? min(animationStep_, static_cast<int>(pixels.size()))
                 : static_cast<int>(pixels.size());
    for (int i = 0; i < pixelLimit; ++i) {
      double x = pixels[i].x;
      double y = pixels[i].y;
      drawQuad(x - 0.42, y - 0.42, x + 0.42, y + 0.42, PIXEL_FILL);
      drawRectangleLines(x - 0.42, y - 0.42, x + 0.42, y + 0.42, PIXEL_EDGE);
    }

    glLineWidth(2.0f);
    drawLine(c.p1.x, c.p1.y, c.p2.x, c.p2.y, ORIGINAL_LINE);
    glLineWidth(3.0f);
    drawRectangleLines(w.xmin, w.ymin, w.xmax, w.ymax, CLIP_WINDOW);
    drawQuad(c.p1.x - 0.16, c.p1.y - 0.16, c.p1.x + 0.16, c.p1.y + 0.16, TEXT);
    drawQuad(c.p2.x - 0.16, c.p2.y - 0.16, c.p2.x + 0.16, c.p2.y + 0.16, TEXT);
    if (mouseHasFirstPoint_) {
      drawQuad(mouseFirstPoint_.x - 0.28, mouseFirstPoint_.y - 0.28,
               mouseFirstPoint_.x + 0.28, mouseFirstPoint_.y + 0.28,
               PIXEL_FILL);
      drawRectangleLines(mouseFirstPoint_.x - 0.32, mouseFirstPoint_.y - 0.32,
                         mouseFirstPoint_.x + 0.32, mouseFirstPoint_.y + 0.32,
                         TEXT);
    }

    ClipResult clipped = cohenSutherland(c.p1, c.p2, c.clip);
    if (showClipped_ && clipped.accepted) {
      glLineWidth(4.0f);
      drawLine(clipped.p1.x, clipped.p1.y, clipped.p2.x, clipped.p2.y,
               CLIPPED_LINE);
      drawQuad(clipped.p1.x - 0.20, clipped.p1.y - 0.20, clipped.p1.x + 0.20,
               clipped.p1.y + 0.20, CLIPPED_LINE);
      drawQuad(clipped.p2.x - 0.20, clipped.p2.y - 0.20, clipped.p2.x + 0.20,
               clipped.p2.y + 0.20, CLIPPED_LINE);
    } else if (showClipped_ && !clipped.accepted) {
      double mx = (c.p1.x + c.p2.x) * 0.5;
      double my = (c.p1.y + c.p2.y) * 0.5;
      glLineWidth(4.0f);
      drawLine(mx - 0.5, my - 0.5, mx + 0.5, my + 0.5, REJECTED);
      drawLine(mx - 0.5, my + 0.5, mx + 0.5, my - 0.5, REJECTED);
    }
    glLineWidth(1.0f);
  }

  void renderOverlay(int panelWidth) {
    setScreenProjection(width_, height_);
    double panelX = width_ - panelWidth;
    drawQuad(panelX, 0.0, width_, height_, PANEL);
    drawLine(panelX, 0.0, panelX, height_, CLIP_WINDOW);

    const TestCase &c = currentCase();
    ClipResult clipped = cohenSutherland(c.p1, c.p2, c.clip);
    auto pixels = currentPixels();
    int visiblePixels =
        animate_ ? min(animationStep_, static_cast<int>(pixels.size()))
                 : static_cast<int>(pixels.size());

    double x = panelX + 18.0;
    double y = height_ - 22.0;
    drawText(x, y, 3.0, "RASTER CLIP", TEXT);
    y -= 34.0;
    drawText(x, y, 2.0, panelCaseTitle(), MUTED_TEXT);
    y -= 32.0;
    drawText(x, y, 2.0,
             "LINE " + formatPoint(c.p1) + " TO " + formatPoint(c.p2), TEXT);
    y -= 24.0;
    drawText(x, y, 2.0,
             "WINDOW X[" + formatNumber(c.clip.xmin) + "," +
                 formatNumber(c.clip.xmax) + "]",
             TEXT);
    y -= 20.0;
    drawText(x, y, 2.0,
             "Y[" + formatNumber(c.clip.ymin) + "," +
                 formatNumber(c.clip.ymax) + "]",
             TEXT);
    y -= 30.0;
    drawText(x, y, 2.0, "OUT P1 " + outcodeName(clipped.originalOut1), TEXT);
    y -= 22.0;
    drawText(x, y, 2.0, "OUT P2 " + outcodeName(clipped.originalOut2), TEXT);
    y -= 30.0;
    if (clipped.accepted) {
      drawText(x, y, 2.0, "CLIP ACCEPT", CLIPPED_LINE);
      y -= 22.0;
      drawText(x, y, 2.0,
               formatPoint(clipped.p1) + " TO " + formatPoint(clipped.p2),
               CLIPPED_LINE);
    } else {
      drawText(x, y, 2.0, "CLIP REJECTED", REJECTED);
    }
    y -= 36.0;
    drawText(x, y, 2.0,
             "PIXELS " + to_string(visiblePixels) + "/" +
                 to_string(pixels.size()),
             PIXEL_FILL);

    if (editMode_) {
      y -= 34.0;
      drawText(x, y, 2.0, "EDIT WINDOW", CLIP_WINDOW);
      y -= 22.0;
      drawText(x, y, 1.6, "XMIN YMIN XMAX YMAX", MUTED_TEXT);
      y -= 22.0;
      string first = editBuffer_.substr(0, 28);
      drawText(x, y, 1.8, "INPUT " + first, TEXT);
      if (editBuffer_.size() > 28) {
        y -= 20.0;
        drawText(x, y, 1.8, editBuffer_.substr(28, 28), TEXT);
      }
      y -= 22.0;
      drawText(x, y, 1.6, "ENTER APPLY BACKSPACE EDIT", PIXEL_FILL);
    } else if (mouseHasFirstPoint_) {
      y -= 28.0;
      drawText(x, y, 1.8, "P1 " + formatPoint(mouseFirstPoint_), PIXEL_FILL);
      y -= 20.0;
      drawText(x, y, 1.6, "LEFT CLICK P2 RIGHT CANCEL", MUTED_TEXT);
    } else if (!editMessage_.empty()) {
      y -= 28.0;
      drawText(x, y, 1.8, editMessage_, PIXEL_FILL);
    }

    y -= 42.0;
    drawText(x, y, 2.0, "LEGEND", TEXT);
    y -= 26.0;
    drawQuad(x, y - 10.0, x + 22.0, y + 2.0, PIXEL_FILL);
    drawText(x + 34.0, y + 2.0, 2.0, "MIDPOINT PIXEL", TEXT);
    y -= 24.0;
    drawLine(x, y - 4.0, x + 22.0, y - 4.0, ORIGINAL_LINE);
    drawText(x + 34.0, y + 2.0, 2.0, "ORIGINAL LINE", TEXT);
    y -= 24.0;
    drawLine(x, y - 4.0, x + 22.0, y - 4.0, CLIP_WINDOW);
    drawText(x + 34.0, y + 2.0, 2.0, "CLIP WINDOW", TEXT);
    y -= 24.0;
    drawLine(x, y - 4.0, x + 22.0, y - 4.0, CLIPPED_LINE);
    drawText(x + 34.0, y + 2.0, 2.0, "CLIPPED RESULT", TEXT);

    y -= 42.0;
    drawText(x, y, 1.8, "LEFT CLICK P1 THEN P2", MUTED_TEXT);
    y -= 20.0;
    drawText(x, y, 1.8, "RIGHT CLICK CANCEL PICK", MUTED_TEXT);
    y -= 20.0;
    drawText(x, y, 1.8, "1-5 CASES SPACE STEP", MUTED_TEXT);
    y -= 20.0;
    drawText(x, y, 1.8, "E WINDOW S SAVE R RESET", MUTED_TEXT);
    y -= 20.0;
    drawText(x, y, 1.8, "ARROWS PAN +/- ZOOM", MUTED_TEXT);
  }

  void writePng(const fs::path &path, const vector<unsigned char> &rgba,
                int width, int height) {
    FILE *fp = fopen(path.string().c_str(), "wb");
    if (!fp) {
      throw runtime_error("Could not open PNG output file: " +
                               path.string());
    }

    png_structp png = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr,
                                              nullptr, nullptr);
    if (!png) {
      fclose(fp);
      throw runtime_error("png_create_write_struct failed");
    }
    png_infop info = png_create_info_struct(png);
    if (!info) {
      png_destroy_write_struct(&png, nullptr);
      fclose(fp);
      throw runtime_error("png_create_info_struct failed");
    }
    if (setjmp(png_jmpbuf(png))) {
      png_destroy_write_struct(&png, &info);
      fclose(fp);
      throw runtime_error("libpng write failed");
    }

    png_init_io(png, fp);
    png_set_IHDR(png, info, width, height, 8, PNG_COLOR_TYPE_RGBA,
                 PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_DEFAULT,
                 PNG_FILTER_TYPE_DEFAULT);
    png_write_info(png, info);

    vector<png_bytep> rows(height);
    for (int y = 0; y < height; ++y) {
      rows[y] = const_cast<png_bytep>(&rgba[(height - 1 - y) * width * 4]);
    }
    png_write_image(png, rows.data());
    png_write_end(png, nullptr);
    png_destroy_write_struct(&png, &info);
    fclose(fp);
  }
};

void writeResultsLog(const fs::path &outputDir) {
  fs::create_directories(outputDir);
  ofstream log(outputDir / "test_case_results.txt");
  for (size_t i = 0; i < TEST_CASES.size(); ++i) {
    const auto &c = TEST_CASES[i];
    auto pixels = midpointLine(static_cast<int>(round(c.p1.x)),
                               static_cast<int>(round(c.p1.y)),
                               static_cast<int>(round(c.p2.x)),
                               static_cast<int>(round(c.p2.y)));
    auto clip = cohenSutherland(c.p1, c.p2, c.clip);
    log << "Case " << (i + 1) << ": " << c.name << "\n";
    log << "Line: " << formatPoint(c.p1) << " to " << formatPoint(c.p2) << "\n";
    log << "Window: xmin=" << formatNumber(c.clip.xmin)
        << " ymin=" << formatNumber(c.clip.ymin)
        << " xmax=" << formatNumber(c.clip.xmax)
        << " ymax=" << formatNumber(c.clip.ymax) << "\n";
    log << "Original outcodes: P1=" << outcodeName(clip.originalOut1)
        << " P2=" << outcodeName(clip.originalOut2) << "\n";
    if (clip.accepted) {
      log << "Clipped result: " << formatPoint(clip.p1) << " to "
          << formatPoint(clip.p2) << "\n";
    } else {
      log << "Clipped result: rejected\n";
    }
    log << "Midpoint pixel sequence: " << pixelSequence(pixels, 200) << "\n\n";
  }
}

void keyCallback(GLFWwindow *window, int key, int scancode, int action,
                 int mods) {
  (void)scancode;
  (void)mods;
  auto *app = static_cast<App *>(glfwGetWindowUserPointer(window));
  if (app) {
    app->onKey(key, action);
  }
}

void charCallback(GLFWwindow *window, unsigned int codepoint) {
  auto *app = static_cast<App *>(glfwGetWindowUserPointer(window));
  if (app) {
    app->onChar(codepoint);
  }
}

void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods) {
  auto *app = static_cast<App *>(glfwGetWindowUserPointer(window));
  if (app) {
    app->onMouseButton(button, action, mods);
  }
}

void framebufferSizeCallback(GLFWwindow *window, int width, int height) {
  auto *app = static_cast<App *>(glfwGetWindowUserPointer(window));
  if (app) {
    app->setSize(width, height);
  }
}

GLFWwindow *createWindow(int width, int height, bool visible) {
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 2);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_VISIBLE, visible ? GLFW_TRUE : GLFW_FALSE);
  GLFWwindow *window =
      glfwCreateWindow(width, height, "Raster Clip Studio", nullptr, nullptr);
  if (!window) {
    throw runtime_error("Failed to create GLFW window");
  }
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  return window;
}

int main(int argc, char **argv) {
  bool exportMode = false;
  fs::path exportDir = "../Screenshots";
  for (int i = 1; i < argc; ++i) {
    string arg = argv[i];
    if (arg == "--export-screenshots") {
      exportMode = true;
      if (i + 1 < argc) {
        exportDir = argv[++i];
      }
    } else if (arg == "--help") {
      cout << "Raster Clip Studio\n";
      cout << "Run: raster_clip_studio\n";
      cout
          << "Export: raster_clip_studio --export-screenshots ../Screenshots\n";
      return 0;
    }
  }

  if (!glfwInit()) {
    cerr << "Failed to initialize GLFW\n";
    return 1;
  }

  try {
    int width = 1280;
    int height = 820;
    GLFWwindow *window = createWindow(width, height, !exportMode);
    {
      App app(window, width, height);
      glfwSetWindowUserPointer(window, &app);
      glfwSetKeyCallback(window, keyCallback);
      glfwSetCharCallback(window, charCallback);
      glfwSetMouseButtonCallback(window, mouseButtonCallback);
      glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);

      int initialFbWidth = 0;
      int initialFbHeight = 0;
      glfwGetFramebufferSize(window, &initialFbWidth, &initialFbHeight);
      app.setSize(initialFbWidth, initialFbHeight);

      if (exportMode) {
        fs::create_directories(exportDir);
        for (size_t i = 0; i < TEST_CASES.size(); ++i) {
          app.onKey(static_cast<int>(GLFW_KEY_1 + i), GLFW_PRESS);
          app.render();
          ostringstream filename;
          filename << "case_" << setfill('0') << setw(2) << (i + 1)
                   << ".png";
          app.saveScreenshot(exportDir / filename.str());
        }
        writeResultsLog(exportDir);
        cout << "Exported screenshots and log to " << exportDir << "\n";
      } else {
        while (!glfwWindowShouldClose(window)) {
          int fbWidth = 0;
          int fbHeight = 0;
          glfwGetFramebufferSize(window, &fbWidth, &fbHeight);
          app.setSize(fbWidth, fbHeight);
          app.update();
          app.render();
          glfwSwapBuffers(window);
          glfwPollEvents();
        }
      }
    }
    glfwDestroyWindow(window);
  } catch (const exception &ex) {
    cerr << "Error: " << ex.what() << "\n";
    glfwTerminate();
    return 1;
  }

  glfwTerminate();
  return 0;
}
