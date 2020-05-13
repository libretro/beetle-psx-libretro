#pragma once

#include "../atlas/atlas.hpp"
#include <vector>

namespace PSX {
    class RectMatch {
    public:
        RectMatch(int x, int y, int w, int h): x(x), y(y), w(w), h(h) {}
        bool matches(Rect r) {
            return (x == -1 || x== r.x) && (y == -1 || y == r.y) && (w == -1 || w == r.width) && (h == -1 || h == r.height);
        }
        int x = -1;
        int y = -1;
        int w = -1;
        int h = -1;
    private:
    };
};

std::vector<PSX::RectMatch> parse_config_file(const char *path);