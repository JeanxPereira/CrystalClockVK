#pragma once
#include "scene/Frame.hpp"

namespace app {

struct Screen {
    virtual ~Screen() = default;
    virtual void step() = 0;
    virtual scene::Frame frame() = 0;
    virtual bool done() const = 0;
};

}
