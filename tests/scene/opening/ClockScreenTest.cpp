#include <cstdio>

#include "../../Check.hpp"
#include "app/ClockScreen.hpp"

int main(int argc, char** argv) {
    if (argc < 3) return 1;
    app::ClockScreen screen = app::ClockScreen::fromStart(argv[1], argv[2]);
    app::FrameSource& base = screen;
    CHECK(!base.done());
    base.step();
    CHECK(screen.state().scene.scale == 0.0f);
    base.step();
    CHECK(screen.state().scene.scale > 0.0f);
    for (int i = 2; i < 40; ++i) base.step();
    CHECK(screen.state().scene.scale > 0.0f);
    CHECK(!base.frame().passes.empty());
    CHECK(screen.state().counter == 40 && screen.state().mode == 2 && screen.state().overlayLevel == 40);
    std::printf("ClockScreenTest passed\n");
    return 0;
}
