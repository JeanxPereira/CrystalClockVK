#include "app/Engine.hpp"
#include <iostream>

int main(int, char*[]) {
    try {
        Engine engine;
        engine.run();
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return -1;
    }
    return 0;
}
