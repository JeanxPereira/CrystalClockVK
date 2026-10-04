#include <cstdio>

#include "../../Check.hpp"
#include "app/ClockScreen.hpp"

int main(int argc, char** argv) {
    if (argc < 3) return 1;
    app::ClockScreen screen = app::ClockScreen::fromStart(argv[1], argv[2]);
    app::Screen& base = screen;
    CHECK(!base.done());
    for (int i = 0; i < 40; ++i) base.step();
    CHECK(!base.frame().passes.empty());
    CHECK(screen.state().counter == 40 && screen.state().mode == 2 && screen.state().overlayLevel == 40);
    std::printf("ClockScreenTest passed\n");
    return 0;
}
