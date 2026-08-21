#include "icad/materials/library.hpp"

#include <array>

namespace icad::materials {
namespace {

constexpr std::array presets{
    Preset{"CONCRETE", {0.55, 0.56, 0.54, 1.0}, 0.0, 0.92, "aggregate_noise", 1103},
    Preset{"STRUCTURAL_STEEL", {0.32, 0.36, 0.40, 1.0}, 0.95, 0.32,
           "brushed_metal", 2207},
    Preset{"ALUMINUM", {0.72, 0.74, 0.76, 1.0}, 0.90, 0.28, "brushed_metal", 3301},
    Preset{"ASPHALT", {0.08, 0.08, 0.075, 1.0}, 0.0, 0.98, "asphalt_speckle", 4409},
    Preset{"GLASS", {0.65, 0.82, 0.90, 0.35}, 0.0, 0.08, "glass_frost", 5519},
    Preset{"WOOD", {0.45, 0.24, 0.10, 1.0}, 0.0, 0.65, "wood_grain", 6607},
    Preset{"BRICK", {0.48, 0.16, 0.09, 1.0}, 0.0, 0.90, "brick_mortar", 7717},
    Preset{"GRANITE", {0.34, 0.33, 0.31, 1.0}, 0.0, 0.72, "mineral_speckle", 8821},
    Preset{"MARBLE", {0.78, 0.77, 0.73, 1.0}, 0.0, 0.34, "marble_vein", 9901},
    Preset{"COPPER", {0.72, 0.30, 0.14, 1.0}, 1.0, 0.26, "hammered_metal", 10103},
    Preset{"BRASS", {0.67, 0.49, 0.16, 1.0}, 1.0, 0.24, "brushed_metal", 11213},
    Preset{"TITANIUM", {0.43, 0.45, 0.48, 1.0}, 0.95, 0.30, "fine_brush", 12323},
    Preset{"CHROME", {0.82, 0.84, 0.86, 1.0}, 1.0, 0.08, "mirror_micrograin", 13441},
    Preset{"RUSTED_STEEL", {0.38, 0.17, 0.08, 1.0}, 0.65, 0.82, "rust_pitting", 14549},
    Preset{"RUBBER", {0.025, 0.025, 0.025, 1.0}, 0.0, 0.88, "rubber_pebble", 15661},
    Preset{"PLASTIC", {0.18, 0.36, 0.62, 1.0}, 0.0, 0.38, "plastic_micrograin", 16763},
    Preset{"CARBON_FIBER", {0.035, 0.04, 0.045, 1.0}, 0.25, 0.24,
           "carbon_twill", 17881},
    Preset{"CERAMIC", {0.88, 0.86, 0.82, 1.0}, 0.0, 0.18, "ceramic_fleck", 18913},
    Preset{"PLASTER", {0.76, 0.74, 0.68, 1.0}, 0.0, 0.94, "plaster_grain", 19031},
    Preset{"FABRIC", {0.24, 0.32, 0.42, 1.0}, 0.0, 0.96, "woven_fiber", 20147},
    Preset{"LEATHER", {0.28, 0.11, 0.055, 1.0}, 0.0, 0.58, "leather_pore", 21269},
    Preset{"EARTH", {0.20, 0.12, 0.055, 1.0}, 0.0, 1.0, "soil_clump", 22381},
    Preset{"GRASS", {0.10, 0.28, 0.07, 1.0}, 0.0, 0.98, "grass_blade", 23497},
    Preset{"WATER", {0.04, 0.22, 0.34, 0.62}, 0.0, 0.06, "water_ripple", 24611},
    Preset{"ICE", {0.62, 0.82, 0.90, 0.58}, 0.0, 0.12, "ice_crystal", 25717},
    Preset{"EMISSIVE_WHITE", {1.0, 0.96, 0.84, 1.0}, 0.0, 0.15,
           "light_diffuser", 26821},
};

} // namespace

auto find(std::string_view name) -> std::optional<Preset> {
    for (const auto& preset : presets) {
        if (preset.name == name) {
            return preset;
        }
    }
    return std::nullopt;
}

auto all() -> std::span<const Preset> { return presets; }

} // namespace icad::materials
