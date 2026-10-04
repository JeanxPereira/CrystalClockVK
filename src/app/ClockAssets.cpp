#include "app/ClockAssets.hpp"

#include <cstring>
#include <stdexcept>
#include <string>

#include "assets/ClockTextures.hpp"

namespace app {

scene::RodMesh rodMeshOf(const assets::Asset& mesh) {
    if (mesh.kind != assets::AssetKind::Mesh || mesh.data.size() != size_t(mesh.width) * 9 * 16) throw std::runtime_error("rod mesh: wrong size");
    scene::RodMesh out;
    size_t at = 0;
    const auto take = [&](std::vector<scene::Vec4>& list, size_t count) {
        list.resize(count);
        for (scene::Vec4& v : list) {
            std::memcpy(v.data(), mesh.data.data() + at, 16);
            at += 16;
        }
    };
    take(out.positions, size_t(mesh.width) * 4);
    take(out.normals, mesh.width);
    take(out.coordinates, size_t(mesh.width) * 4);
    return out;
}

void uploadClockTextures(render::NativeRenderer& renderer, const assets::AssetSet& set) {
    for (size_t n = 0; n < assets::kClockTextures.size(); ++n) {
        const assets::Asset* texture = set.find(assets::kClockTextures[n].name);
        if (!texture || texture->kind != assets::AssetKind::TextureRgba32) throw std::runtime_error("no clock texture " + std::string(assets::kClockTextures[n].name));
        renderer.setTexture(int32_t(n), texture->width, texture->height, texture->data);
    }
}

}
