// Deswizzle a region of a .gs VRAM freeze to raw RGBA8 (W4 texture bring-up).
// The freeze holds the 4MB GS local memory; this decodes a PSMCT32 region
// (framebuffer or texture) so it can be viewed/validated before wiring textures.
//
// Usage: vramdump <dump.gs> <vramOffset> <vramByteBase> <W> <H> [out.rgba]
//   vramOffset   = byte offset of the 4MB VRAM block within the freeze
//   vramByteBase = byte offset of the region inside VRAM (TBP0*256 / FBP*8192)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "GsDumpParser.hpp"
#include "gs/SwizzleEngine.hpp"

int main(int argc, char** argv) {
    if (argc < 6) {
        std::fprintf(stderr,
            "usage: vramdump <dump.gs> <vramOffset> <vramByteBase> <W> <H> [out.rgba]\n");
        return 2;
    }
    const std::string path = argv[1];
    const long vramOffset = std::atol(argv[2]);
    const long byteBase = std::atol(argv[3]);
    const int W = std::atoi(argv[4]);
    const int H = std::atoi(argv[5]);
    const std::string out = argc > 6 ? argv[6] : "vram.rgba";

    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) { std::fprintf(stderr, "cannot open %s\n", path.c_str()); return 1; }
    const std::streamsize n = in.tellg();
    in.seekg(0);
    std::vector<uint8_t> buf(static_cast<size_t>(n));
    in.read(reinterpret_cast<char*>(buf.data()), n);

    GsCommandStream s;
    try {
        s = GsDumpParser::parse(buf.data(), buf.size());
    } catch (const std::exception& e) {
        std::fprintf(stderr, "parse error: %s\n", e.what());
        return 1;
    }

    const size_t need = static_cast<size_t>(W) * H * 4;  // PSMCT32 source bytes
    const size_t start = static_cast<size_t>(vramOffset) + static_cast<size_t>(byteBase);
    if (start + need > s.freeze.size()) {
        std::fprintf(stderr, "out of range: freeze=%zu start=%zu need=%zu\n",
                     s.freeze.size(), start, need);
        return 1;
    }

    const std::vector<uint8_t> rgba =
        SwizzleEngine::deswizzle(s.freeze.data() + start, W, H, W, GsPixelFormat::PSMCT32, nullptr);

    std::ofstream o(out, std::ios::binary);
    o.write(reinterpret_cast<const char*>(rgba.data()), rgba.size());
    std::fprintf(stderr, "freeze=%zu  decoded %dx%d PSMCT32 @ vram+%ld+%ld -> %s (%zu bytes)\n",
                 s.freeze.size(), W, H, vramOffset, byteBase, out.c_str(), rgba.size());
    return 0;
}
