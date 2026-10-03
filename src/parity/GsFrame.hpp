#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace scene {
enum class Primitive { Triangles, Sprites, Lines };
enum class BlendTerm { Source, Destination, Zero };
enum class BlendFactor { SourceAlpha, DestinationAlpha, Fixed };
enum class DepthTest { Never, Always, GreaterEqual, Greater };
enum class AddressMode { Repeat, Clamp, RegionClamp, RegionRepeat };
enum class Filter { Nearest, Bilinear };
enum class Coordinates { Texel, Projective };
struct Blend { BlendTerm a, b; BlendFactor c; BlendTerm d; uint8_t fixed; };
struct Depth { DepthTest test; bool write; };
struct Address { AddressMode mode; int32_t min, max; };
struct TextureAlpha { bool constant; uint8_t value; bool zeroWhenBlack; };
struct Texture { std::string source; bool sourceIsTarget; uint32_t width, height; Coordinates coordinates; Address addressU, addressV; Filter filter; TextureAlpha alpha; };
struct Scissor { int32_t x0, y0, x1, y1; };
struct Vertex { float x, y; uint32_t depth; float r, g, b, a; float s, t, q; };
struct Pass { uint32_t index; std::string name, target; Primitive primitive; Scissor scissor; std::optional<Blend> blend; bool antialias; Depth depth; std::optional<Texture> texture; std::string skip; std::vector<Vertex> vertices; };
struct Target { std::string id; uint32_t width, height; };
struct FrameDescription { uint32_t field; std::vector<Target> targets; std::vector<Pass> passes; };
}
