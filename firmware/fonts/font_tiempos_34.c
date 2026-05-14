// Stub: aliases font_styrene_28 until the proprietary TiemposText OTF is available.
// Replace by running: lv_font_conv --font TiemposText-400-Regular.otf --size 34 ...
#include "lvgl.h"

extern const lv_font_t font_styrene_28;
// Expose as font_tiempos_34 — same glyph data, will render in Styrene B until replaced
const lv_font_t font_tiempos_34 = {
    .get_glyph_dsc    = NULL,  // Use the alias approach via lv_font_set_fallback
    .get_glyph_bitmap = NULL,
    .line_height      = 28,
    .base_line        = 6,
    .subpx            = 0,
    .underline_position  = -2,
    .underline_thickness = 1,
    .fallback         = &font_styrene_28,
};
