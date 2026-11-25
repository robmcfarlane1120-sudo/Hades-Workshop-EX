#pragma once
#include "Fields.h"
#include "TIMImages.h"
#include "Database_Steam.h"
#include <wx/filename.h>
#include <memory>
#include <stdexcept>
#include <algorithm>

// Preview-owned snapshots: never change the project's atlas, overlays or animation state.
class FieldAnimationPreview {
    struct Track { FieldTilesAnimDataStruct data; int frame = 0, wait = 0, rate = 256; };
    struct Tile { int overlay, x, y, source, depth; TIM_BlendMode blend; };
    std::vector<Track> tracks;
    std::vector<Tile> drawOrder;
    std::vector<bool> baseVisible;
    std::vector<uint32_t> atlas, pixels;
    int width = 0, height = 0, atlasWidth = 0, atlasHeight = 0, tileSize = 64;
    static void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
public:
    bool HasAnimation() const { return !tracks.empty(); }
    void Load(FieldTilesDataStruct& field, ConfigurationSet& config, int fieldId, int cameraId) {
        Require(!field.IsBGXFormat(), "BGX fields use the regular HWS preview.");
        Require(cameraId >= 0 && cameraId < (int)field.camera.size(), "No valid background camera.");
        const SteamFieldDictionary* entry = nullptr;
        for (const auto& candidate : SteamFieldScript) if (candidate.script_id == fieldId) { entry = &candidate; break; }
        Require(entry && entry->file_id >= 1 && entry->file_id <= 9, "No field mapping.");
        wxFileName gameRoot = wxFileName::DirName(wxString::FromUTF8(config.steam_dir_assets));
        gameRoot.RemoveLastDir();
        const wxString mod = gameRoot.GetPathWithSep() + "MoguriMain\\StreamingAssets\\";
        const std::string map = wxString::FromUTF8(entry->background_name).Lower().ToStdString();
        const std::string resource = "assets/resources/fieldmaps/" + map + "/";
        // Modified layouts must not be paired with the vanilla atlas ordering.
        // Until those layouts are loaded independently, use the established HWS fallback.
        Require(!wxFileExists(mod + wxString::FromUTF8(resource + map + ".bgs.bytes")),
            "This field has a Moguri layout override; using regular HWS preview.");
        wxImage loose;
        const wxString loosePath = mod + wxString::FromUTF8(resource + "atlas.png");
        const wxString moguriBundle = mod + wxString::Format("p0data%d.bin", entry->file_id + 10);
        const bool useMoguri = wxFileExists(loosePath) || wxFileExists(moguriBundle);
        tileSize = useMoguri ? 64 : 32;
        const int stride = tileSize + 4;
        const int scale = tileSize / 16;
        if (wxFileExists(loosePath)) {
            Require(loose.LoadFile(loosePath, wxBITMAP_TYPE_PNG), "Cannot read Moguri atlas PNG.");
            atlasWidth = loose.GetWidth(); atlasHeight = loose.GetHeight();
            Require(atlasWidth > 0 && atlasHeight > 0 && (uint64_t)atlasWidth * atlasHeight <= 67108864, "Atlas is too large.");
            atlas.resize((size_t)atlasWidth * atlasHeight);
            const auto* rgb = loose.GetData(); const auto* alpha = loose.HasAlpha() ? loose.GetAlpha() : nullptr;
            for (size_t i = 0; i < atlas.size(); ++i)
                atlas[i] = ((uint32_t)(alpha ? alpha[i] : 255) << 24) | (rgb[3*i] << 16) | (rgb[3*i+1] << 8) | rgb[3*i+2];
        } else {
            const wxString bundlePath = useMoguri ? moguriBundle : wxFileName::DirName(wxString::FromUTF8(config.steam_dir_assets)).GetPathWithSep() + wxString::Format("p0data%d.bin", entry->file_id + 10);
            std::fstream input(bundlePath.ToStdWstring(), std::ios::in | std::ios::binary);
            Require(input.is_open(), "Field texture bundle not found beside the opened game.");
            UnityArchiveMetaData meta;
            meta.Load(input, (UnityArchiveFile)(UNITY_ARCHIVE_DATA11 + entry->file_id - 1));
            Require(input.good() && meta.loaded, "Cannot read field bundle metadata.");
            const auto bundleOffset = meta.GetFileOffset("", 142);
            Require(bundleOffset != 0, "field bundle index missing.");
            input.seekg(bundleOffset);
            UnityArchiveAssetBundle bundle; bundle.Load(input);
            Require(input.good(), "Cannot read field bundle index.");
            int index = meta.GetFileIndexByInfo(bundle.GetFileInfo(resource + "atlas.png"), 28);
            Require(index >= 0, "No field atlas for this field.");
            auto offset = meta.GetFileOffsetByIndex(index);
            input.seekg(0, std::ios::end); const auto fileEnd = input.tellg();
            Require(offset && (uint64_t)offset + 60 <= (uint64_t)fileEnd, "Invalid atlas offset.");
            input.seekg(offset);
            uint32_t header[15] = {}; input.read((char*)header, sizeof(header));
            Require(input.good() && header[0] && header[1] && header[0] <= 16384 && header[1] <= 16384 && (uint64_t)header[0]*header[1] <= 67108864, "Invalid atlas dimensions.");
            auto bytes = TIMImageDataStruct::GetSteamTextureFileSize(header[0], header[1], header[3]);
            Require(bytes >= 60 && bytes <= 268435456 && (uint64_t)offset + bytes <= (uint64_t)fileEnd && bytes <= meta.GetFileSizeByIndex(index), "Invalid or unsupported atlas texture.");
            std::vector<uint8_t> encoded(bytes);
            input.seekg(offset); input.read((char*)encoded.data(), bytes);
            Require(input.good(), "Truncated field atlas.");
            uint32_t w = 0, h = 0; uint8_t* decoded = nullptr;
            Require(TIMImageDataStruct::ConvertFromSteamTexture(encoded.data(), &w, &h, &decoded), "Cannot decode field atlas.");
            std::unique_ptr<uint8_t[]> owned(decoded);
            atlasWidth = w; atlasHeight = h; atlas.resize((size_t)w*h);
            for (size_t i = 0; i < atlas.size(); ++i)
                atlas[i] = ((uint32_t)decoded[4*i+3] << 24) | (decoded[4*i] << 16) | (decoded[4*i+1] << 8) | decoded[4*i+2];
        }
        // Vanilla 32px and Moguri 64px tiles both have two pixels of padding per side.
        Require(atlasWidth >= stride && atlasHeight >= stride && (!useMoguri || (atlasWidth % stride == 0 && atlasHeight % stride == 0)), "Atlas does not match the expected padded tile size.");
        const auto& cam = field.camera[cameraId];
        int maxX = 0, maxY = 0;
        baseVisible.resize(field.tiles_amount, false);
        for (size_t i = 0; i < field.tiles_amount; ++i) {
            const auto& layer = field.tiles.at(i);
            baseVisible[i] = layer.camera_id == cameraId && (layer.is_static || layer.is_first_of_anim);
            if (layer.camera_id != cameraId) continue;
            Require(!layer.is_memoria_bgx, "Custom image layers use regular HWS preview.");
            for (const auto& tile : layer.tile) {
                if (tile.res == 0) continue; // Match HWS's normal preview filtering of mask tiles.
                const int sx = (tile.steam_id % (atlasWidth / stride)) * stride + 2;
                const int sy = (tile.steam_id / (atlasWidth / stride)) * stride + 2;
                Require(sx + tileSize <= atlasWidth && sy + tileSize <= atlasHeight, "Tile lies outside Moguri atlas.");
                int x = (layer.pos_x + tile.pos_x - cam.pos_x) * scale;
                int y = (layer.pos_y + tile.pos_y - cam.pos_y) * scale;
                Require(x >= 0 && y >= 0 && x <= 16384 && y <= 16384, "Invalid tile placement.");
                maxX = std::max(maxX, x + tileSize); maxY = std::max(maxY, y + tileSize);
                TIM_BlendMode blend = tile.trans ? (TIM_BlendMode)(TIM_BLENDMODE_ABR_0 + tile.alpha) : TIM_BLENDMODE_ABR_NONE;
                if (fieldId == 2259 && i == 14) blend = TIM_BLENDMODE_ABR_NONE;
                drawOrder.push_back({(int)i, x, y, sy * atlasWidth + sx, (int)layer.depth + tile.depth, blend});
            }
        }
        Require(maxX && maxY && (uint64_t)maxX * maxY <= 16777216, "Preview dimensions exceed limit.");
        width = maxX; height = maxY; pixels.resize((size_t)width * height);
        std::stable_sort(drawOrder.begin(), drawOrder.end(), [](const Tile& a, const Tile& b) { return a.depth > b.depth; });
        for (const auto& animation : field.anim) {
            if (animation.camera_id != cameraId || animation.tile_list.size() < 2) continue;
            Require(animation.tile_list.size() <= 65536 && animation.tile_duration.size() == animation.tile_list.size(), "Invalid animation sequence.");
            for (auto target : animation.tile_list) Require(target < baseVisible.size() && field.tiles[target].camera_id == cameraId, "Invalid animation overlay.");
            Track track; track.data = animation; track.rate = animation.rate ? animation.rate : 256;
            track.frame = animation.default_frame < animation.tile_list.size()*256 ? animation.default_frame : 0;
            tracks.push_back(track);
        }
    }
    wxBitmap Render(int ticks) {
        auto visible = baseVisible;
        // Match Memoria's sequential track precedence for shared overlays.
        for (auto& track : tracks) {
            const int count = (int)track.data.tile_list.size();
            int frame = track.frame >> 8;
            for (int tick = 0; tick < ticks; ++tick) {
                frame = track.frame >> 8;
                if (++track.wait >= std::max(1, (int)(int8_t)track.data.tile_duration[frame])) {
                track.wait = 0; track.frame += track.rate;
                if (track.frame >= count * 256 || track.frame < 0) {
                    if (track.data.flag & 32) { track.frame = track.frame < 0 ? 0 : (count-1)*256; track.rate = -track.rate; }
                    else track.frame = track.frame < 0 ? (count-1)*256 : 0;
                }
                }
            }
            for (auto target : track.data.tile_list) visible[target] = false;
            visible[track.data.tile_list[track.frame >> 8]] = true;
        }
        std::fill(pixels.begin(), pixels.end(), 0);
        for (const auto& tile : drawOrder) if (visible[tile.overlay]) {
            for (int y = 0; y < tileSize; ++y) for (int x = 0; x < tileSize; ++x) {
                uint32_t pixel = atlas[tile.source + y * atlasWidth + x];
                if (pixel >> 24) {
                    auto& dest = pixels[(tile.y+y)*width + tile.x+x];
                    dest = ((tile.blend == TIM_BLENDMODE_ABR_NONE || tile.blend == TIM_BLENDMODE_ABR_0) && (pixel >> 24) == 255) ? pixel : ImageMergePixels(dest, pixel, tile.blend);
                }
            }
        }
        wxImage image(width, height, false); image.InitAlpha();
        auto* rgb = image.GetData(); auto* alpha = image.GetAlpha();
        for (size_t i = 0; i < pixels.size(); ++i) {
            rgb[3*i] = pixels[i] >> 16; rgb[3*i+1] = pixels[i] >> 8; rgb[3*i+2] = pixels[i]; alpha[i] = pixels[i] >> 24;
        }
        return wxBitmap(image);
    }
};
