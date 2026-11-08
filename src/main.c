
#include <Raven.h>
#include "../Dependencies/qrgen/qrcodegen.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "hello_xml.h"
#include "raven/raven_core.h"
#include "raven/raven_dialogs.h"

/* Raven widgets. */
static Rid window_id = R_INVALID_ID;
static Rid input_text = R_INVALID_ID;
static Rid cb_low = R_INVALID_ID;
static Rid cb_med = R_INVALID_ID;
static Rid cb_qua = R_INVALID_ID;
static Rid cb_high = R_INVALID_ID;
static Rid sp_border = R_INVALID_ID;
static Rid sp_scale = R_INVALID_ID;
static Rid input_light = R_INVALID_ID;
static Rid input_dark = R_INVALID_ID;
static Rid sp_vermin = R_INVALID_ID;
static Rid sp_vermax = R_INVALID_ID;
static Rid sp_maskpattern = R_INVALID_ID;
static Rid cb_boostecc = R_INVALID_ID;
static Rid qr_canvas = R_INVALID_ID;
static Rid status_label = R_INVALID_ID;

typedef struct QRColor {
    float r;
    float g;
    float b;
    float a;
} QRColor;

/* Persistent QR data: the paint callback runs after the Generate action. */
static uint8_t qr_code[qrcodegen_BUFFER_LEN_MAX];
static int qr_size = 0;
static int qr_border = 8;
static float requested_module_size = 8.0f;
static bool qr_ready = false;
static QRColor light_color = {1.0f, 1.0f, 1.0f, 1.0f};
static QRColor dark_color = {0.0f, 0.0f, 0.0f, 1.0f};

static void set_status(const char *message) {
    if (status_label != R_INVALID_ID)
        Rlabel_set_text(status_label, message);
}

static char *copy_string(const char *value) {
    if (!value) value = "";
    const size_t len = strlen(value);
    char *copy = (char *)malloc(len + 1);
    if (copy) memcpy(copy, value, len + 1);
    return copy;
}

static int hex_digit_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* Accepts #RGB, #RGBA, #RRGGBB, or #RRGGBBAA. */
static bool parse_color(const char *text, QRColor *out) {
    if (!text || !out) return false;

    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r')
        ++text;
    if (*text == '#') ++text;

    size_t len = strlen(text);
    while (len > 0 && (text[len - 1] == ' ' || text[len - 1] == '\t' ||
                       text[len - 1] == '\n' || text[len - 1] == '\r'))
        --len;

    if (len != 3 && len != 4 && len != 6 && len != 8)
        return false;

    int values[8];
    for (size_t i = 0; i < len; ++i) {
        values[i] = hex_digit_value(text[i]);
        if (values[i] < 0) return false;
    }

    if (len == 3 || len == 4) {
        out->r = (float)(values[0] * 17) / 255.0f;
        out->g = (float)(values[1] * 17) / 255.0f;
        out->b = (float)(values[2] * 17) / 255.0f;
        out->a = len == 4 ? (float)(values[3] * 17) / 255.0f : 1.0f;
    } else {
        out->r = (float)(values[0] * 16 + values[1]) / 255.0f;
        out->g = (float)(values[2] * 16 + values[3]) / 255.0f;
        out->b = (float)(values[4] * 16 + values[5]) / 255.0f;
        out->a = len == 8
            ? (float)(values[6] * 16 + values[7]) / 255.0f
            : 1.0f;
    }
    return true;
}


/* -------------------------------------------------------------------------
 * Minimal RGBA PNG writer.
 *
 * This uses DEFLATE stored blocks, so it needs no zlib or stb dependency.
 * It is intentionally simple: QR images are small, and PNG remains lossless.
 * ------------------------------------------------------------------------- */
static void png_write_u32_be(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static void png_write_u16_le(uint8_t *p, uint16_t value) {
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static uint32_t png_crc_table[256];
static bool png_crc_table_ready = false;

static void png_init_crc_table(void) {
    if (png_crc_table_ready) return;

    for (uint32_t n = 0; n < 256; ++n) {
        uint32_t c = n;
        for (int k = 0; k < 8; ++k)
            c = (c & 1U) ? (0xEDB88320U ^ (c >> 1)) : (c >> 1);
        png_crc_table[n] = c;
    }
    png_crc_table_ready = true;
}

static uint32_t png_crc32(const uint8_t *data, size_t length) {
    png_init_crc_table();
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < length; ++i)
        crc = png_crc_table[(crc ^ data[i]) & 0xFFU] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFU;
}

static uint8_t color_channel_to_byte(float channel) {
    if (channel < 0.0f) channel = 0.0f;
    if (channel > 1.0f) channel = 1.0f;
    return (uint8_t)(channel * 255.0f + 0.5f);
}

/* Returns one byte of the PNG's unfiltered scanline stream. Each row begins
 * with PNG filter byte 0; the remaining bytes are RGBA pixels. */
static uint8_t qr_png_raw_byte(
    size_t raw_index,
    size_t row_stride,
    int scale,
    const QRColor *light,
    const QRColor *dark
) {
    const size_t row = raw_index / row_stride;
    const size_t column = raw_index % row_stride;
    if (column == 0) return 0; /* PNG filter: None */

    const size_t pixel_byte = column - 1;
    const int pixel_x = (int)(pixel_byte / 4);
    const int channel = (int)(pixel_byte % 4);
    const int module_x = pixel_x / scale;
    const int module_y = (int)row / scale;
    const QRColor *color = qrcodegen_getModule(qr_code, module_x, module_y)
        ? dark : light;

    switch (channel) {
        case 0: return color_channel_to_byte(color->r);
        case 1: return color_channel_to_byte(color->g);
        case 2: return color_channel_to_byte(color->b);
        default: return color_channel_to_byte(color->a);
    }
}

static bool save_qr_png(const char *path, int scale,
                        const QRColor *light, const QRColor *dark) {
    if (!path || !path[0] || !qr_ready || qr_size <= 0 || scale <= 0 ||
        !light || !dark)
        return false;

    /* qrcodegen's maximum matrix is 177 modules; validate arithmetic anyway. */
    if (qr_size > INT32_MAX / scale)
        return false;

    const int width = qr_size * scale;
    const int height = width;
    if ((size_t)width > (SIZE_MAX - 1U) / 4U)
        return false;

    const size_t row_stride = 1U + (size_t)width * 4U;
    if ((size_t)height > SIZE_MAX / row_stride)
        return false;
    const size_t raw_size = row_stride * (size_t)height;

    const size_t block_count = raw_size / 65535U + ((raw_size % 65535U) != 0U);
    if (block_count > (SIZE_MAX - raw_size - 6U) / 5U)
        return false;
    const size_t zlib_size = 2U + raw_size + block_count * 5U + 4U;
    if (zlib_size > UINT32_MAX || zlib_size > SIZE_MAX - 57U)
        return false;
    const size_t png_size = 57U + zlib_size;

    uint8_t *png = (uint8_t *)malloc(png_size);
    if (!png) return false;

    static const uint8_t signature[8] = {
        137, 80, 78, 71, 13, 10, 26, 10
    };
    memcpy(png, signature, sizeof(signature));
    size_t offset = 8U;

    /* IHDR: 8-bit RGBA, no interlace. */
    uint8_t ihdr[13];
    png_write_u32_be(ihdr + 0, (uint32_t)width);
    png_write_u32_be(ihdr + 4, (uint32_t)height);
    ihdr[8] = 8;
    ihdr[9] = 6;
    ihdr[10] = 0;
    ihdr[11] = 0;
    ihdr[12] = 0;
    png_write_u32_be(png + offset, 13U);
    memcpy(png + offset + 4U, "IHDR", 4U);
    memcpy(png + offset + 8U, ihdr, sizeof(ihdr));
    png_write_u32_be(png + offset + 21U,
                     png_crc32(png + offset + 4U, 17U));
    offset += 25U;

    /* IDAT payload: zlib header + uncompressed DEFLATE blocks + Adler-32. */
    png_write_u32_be(png + offset, (uint32_t)zlib_size);
    memcpy(png + offset + 4U, "IDAT", 4U);
    uint8_t *z = png + offset + 8U;
    size_t zpos = 0;
    z[zpos++] = 0x78; /* zlib, 32K window */
    z[zpos++] = 0x01; /* fastest/no-compression profile */

    uint32_t adler_a = 1U;
    uint32_t adler_b = 0U;
    size_t raw_offset = 0U;

    while (raw_offset < raw_size) {
        const size_t remaining = raw_size - raw_offset;
        const uint16_t block_length = (uint16_t)(remaining > 65535U
            ? 65535U : remaining);
        const bool final_block = (remaining <= 65535U);

        z[zpos++] = final_block ? 0x01 : 0x00; /* stored block, byte-aligned */
        png_write_u16_le(z + zpos, block_length);
        zpos += 2U;
        png_write_u16_le(z + zpos, (uint16_t)~block_length);
        zpos += 2U;

        for (uint32_t i = 0; i < block_length; ++i) {
            const uint8_t value = qr_png_raw_byte(
                raw_offset, row_stride, scale, light, dark);
            z[zpos++] = value;
            adler_a += value;
            if (adler_a >= 65521U) adler_a -= 65521U;
            adler_b += adler_a;
            adler_b %= 65521U;
            ++raw_offset;
        }
    }

    const uint32_t adler = (adler_b << 16) | adler_a;
    png_write_u32_be(z + zpos, adler);
    zpos += 4U;

    /* zpos is expected to equal zlib_size; keep the check for safety. */
    if (zpos != zlib_size) {
        free(png);
        return false;
    }
    png_write_u32_be(png + offset + 8U + zlib_size,
                     png_crc32(png + offset + 4U, zlib_size + 4U));
    offset += 12U + zlib_size;

    /* IEND. */
    png_write_u32_be(png + offset, 0U);
    memcpy(png + offset + 4U, "IEND", 4U);
    png_write_u32_be(png + offset + 8U, png_crc32(png + offset + 4U, 4U));
    offset += 12U;

    const bool written = (offset == png_size) &&
        (Rfile_write(path, png, png_size) != 0);
    free(png);
    return written;
}

static bool read_selected_colors(QRColor *light, QRColor *dark) {
    *light = (QRColor){1.0f, 1.0f, 1.0f, 1.0f};
    *dark = (QRColor){0.0f, 0.0f, 0.0f, 1.0f};

    char *light_text = copy_string(Rtext_input_get_text(input_light));
    char *dark_text = copy_string(Rtext_input_get_text(input_dark));
    if (!light_text || !dark_text) {
        free(light_text);
        free(dark_text);
        return false;
    }

    const bool light_ok = light_text[0] == '\0' || parse_color(light_text, light);
    const bool dark_ok = dark_text[0] == '\0' || parse_color(dark_text, dark);
    free(light_text);
    free(dark_text);
    return light_ok && dark_ok;
}


/* Draws the last successfully generated QR code into Raven's Custom widget. */
static void paint_qr(Rid widget, RPainter *painter,
                     float width, float height, void *user_data) {
    (void)widget;
    (void)user_data;

    /* Background also provides the QR code's quiet zone. */
    Rdraw_rect(painter, 0.0f, 0.0f, width, height, 0.0f,
               light_color.r, light_color.g, light_color.b, light_color.a);

    if (!qr_ready || qr_size <= 0 || width <= 0.0f || height <= 0.0f)
        return;

    const float total_modules = (float)(qr_size + 2 * qr_border);
    const float fit_scale = (width < height ? width : height) / total_modules;
    const float module_size = requested_module_size < fit_scale
        ? requested_module_size
        : fit_scale;

    if (module_size <= 0.0f) return;

    const float drawn_size = total_modules * module_size;
    const float origin_x = (width - drawn_size) * 0.5f;
    const float origin_y = (height - drawn_size) * 0.5f;

    /* Draw consecutive dark modules in a row as one rectangle. */
    for (int y = 0; y < qr_size; ++y) {
        int x = 0;
        while (x < qr_size) {
            while (x < qr_size && !qrcodegen_getModule(qr_code, x, y))
                ++x;
            if (x >= qr_size) break;

            const int run_start = x;
            while (x < qr_size && qrcodegen_getModule(qr_code, x, y))
                ++x;

            const float draw_x = origin_x + (float)(qr_border + run_start) * module_size;
            const float draw_y = origin_y + (float)(qr_border + y) * module_size;
            const float draw_w = (float)(x - run_start) * module_size;

            Rdraw_rect(painter, draw_x, draw_y, draw_w, module_size, 0.0f,
                       dark_color.r, dark_color.g, dark_color.b, dark_color.a);
        }
    }
}

static enum qrcodegen_Ecc selected_ecc(void) {
    if (Rcheckbox_is_checked(cb_low)) return qrcodegen_Ecc_LOW;
    if (Rcheckbox_is_checked(cb_med)) return qrcodegen_Ecc_MEDIUM;
    if (Rcheckbox_is_checked(cb_qua)) return qrcodegen_Ecc_QUARTILE;
    return qrcodegen_Ecc_HIGH;
}

static void on_generate_action(Rid sender, const char *action, void *user_data) {
    (void)sender;
    (void)action;
    (void)user_data;

    const char *input = Rtext_input_get_text(input_text);
    if (!input || input[0] == '\0') {
        set_status("Enter some text or a URL first.");
        return;
    }

    /* Copy the text before making more Raven API calls. */
    const size_t text_len = strlen(input);
    char *text = (char *)malloc(text_len + 1);
    if (!text) {
        set_status("Not enough memory to generate the QR code.");
        return;
    }
    memcpy(text, input, text_len + 1);

    const int min_version = (int)Rspin_box_get_value(sp_vermin);
    const int max_version = (int)Rspin_box_get_value(sp_vermax);
    const int border = (int)Rspin_box_get_value(sp_border);
    const double scale = Rspin_box_get_value(sp_scale);
    const int mask_value = (int)Rspin_box_get_value(sp_maskpattern);

    if (min_version < qrcodegen_VERSION_MIN ||
        max_version > qrcodegen_VERSION_MAX ||
        min_version > max_version) {
        free(text);
        set_status("Version range must satisfy 1 <= minimum <= maximum <= 40.");
        return;
    }
    if (border < 0 || scale <= 0.0 || mask_value < -1 || mask_value > 7) {
        free(text);
        set_status("Check the border, scale, and mask values.");
        return;
    }

    QRColor new_light = {1.0f, 1.0f, 1.0f, 1.0f};
    QRColor new_dark = {0.0f, 0.0f, 0.0f, 1.0f};
    char *light_text = copy_string(Rtext_input_get_text(input_light));
    char *dark_text = copy_string(Rtext_input_get_text(input_dark));
    if (!light_text || !dark_text) {
        free(text);
        free(light_text);
        free(dark_text);
        set_status("Not enough memory to read the color settings.");
        return;
    }

    if (light_text[0] != '\0' && !parse_color(light_text, &new_light)) {
        free(text);
        free(light_text);
        free(dark_text);
        set_status("Light color must be #RGB, #RGBA, #RRGGBB, or #RRGGBBAA.");
        return;
    }
    if (dark_text[0] != '\0' && !parse_color(dark_text, &new_dark)) {
        free(text);
        free(light_text);
        free(dark_text);
        set_status("Dark color must be #RGB, #RGBA, #RRGGBB, or #RRGGBBAA.");
        return;
    }
    free(light_text);
    free(dark_text);

    const enum qrcodegen_Ecc ecc = selected_ecc();
    const enum qrcodegen_Mask mask = (enum qrcodegen_Mask)mask_value;
    const bool boost_ecc = Rcheckbox_is_checked(cb_boostecc) != 0;

    uint8_t temp_buffer[qrcodegen_BUFFER_LEN_MAX];
    qr_ready = false;

    const bool ok = qrcodegen_encodeText(
        text,
        temp_buffer,
        qr_code,
        ecc,
        min_version,
        max_version,
        mask,
        boost_ecc
    );
    free(text);

    if (!ok) {
        Rwidget_invalidate(qr_canvas);
        set_status("Could not encode that text in the selected version/ECC range.");
        return;
    }

    qr_size = qrcodegen_getSize(qr_code);
    qr_border = border;
    requested_module_size = (float)scale;
    light_color = new_light;
    dark_color = new_dark;
    qr_ready = true;

    Rwidget_invalidate(qr_canvas);

    char message[128];
    const int version = (qr_size - 17) / 4;
    (void)snprintf(message, sizeof(message),
                   "Generated: version %d, %d x %d modules.",
                   version, qr_size, qr_size);
    set_status(message);
}


static void on_save_qr_action(Rid sender, const char *action, void *user_data) {
    (void)sender;
    (void)action;
    (void)user_data;

    if (!qr_ready) {
        set_status("Generate a QR code before saving it.");
        return;
    }

    const int scale = (int)Rspin_box_get_value(sp_scale);
    if (scale <= 0) {
        set_status("Scale must be greater than zero.");
        return;
    }

    QRColor export_light;
    QRColor export_dark;
    if (!read_selected_colors(&export_light, &export_dark)) {
        set_status("Check the Light and Dark colors. Use #RGB, #RGBA, #RRGGBB, or #RRGGBBAA.");
        return;
    }

    static const RFileFilter filters[] = {
        { "PNG image", "*.png" }
    };
    const RFileDialogDesc dialog = {
        .parent = window_id,
        .title = "Save QR code",
        .file_name = "qrcode.png",
        .filters = filters,
        .filter_count = 1,
        .default_extension = "png"
    };

    /* Raven returns NULL when cancelled; path remains valid until the next dialog. */
    const char *path = Rfile_dialog_save(&dialog);
    if (!path) {
        set_status("Save cancelled.");
        return;
    }

    if (!save_qr_png(path, scale, &export_light, &export_dark)) {
        set_status("Could not save the PNG. Check the destination and available disk space.");
        return;
    }

    /* Keep the on-screen preview consistent with the settings just exported. */
    light_color = export_light;
    dark_color = export_dark;
    requested_module_size = (float)scale;
    Rwidget_invalidate(qr_canvas);

    char message[256];
    (void)snprintf(message, sizeof(message),
                   "Saved %s (%d x %d pixels, no border).",
                   path, qr_size * scale, qr_size * scale);
    set_status(message);
}

static bool widgets_are_valid(void) {
    const Rid widgets[] = {
        input_text, cb_low, cb_med, cb_qua, cb_high,
        sp_border, sp_scale, input_light, input_dark,
        sp_vermin, sp_vermax, sp_maskpattern, cb_boostecc,
        qr_canvas, status_label
    };

    for (size_t i = 0; i < sizeof(widgets) / sizeof(widgets[0]); ++i) {
        if (widgets[i] == R_INVALID_ID) return false;
    }
    return true;
}

#ifdef _WIN32
#include <windows.h>

#ifdef NDEBUG
int WINAPI WinMain(
    HINSTANCE instance,
    HINSTANCE previous_instance,
    LPSTR command_line,
    int show_command)
{
    return main();
}
#endif
#endif

static enum qrcodegen_Ecc current_ecc = qrcodegen_Ecc_HIGH;
static bool updating_ecc = false;

static void on_ecc_toggled(Rid sender, const char* action, void* user_data) {
    (void)action;
    (void)user_data;

    if(updating_ecc) return;

    enum qrcodegen_Ecc selected_ecc;
    Rid selected_checkbox;

    if (sender == cb_low) {
        selected_checkbox = cb_low;
        selected_ecc = qrcodegen_Ecc_LOW;
    } else if (sender == cb_med) {
        selected_checkbox = cb_med;
        selected_ecc = qrcodegen_Ecc_MEDIUM;
    } else if (sender == cb_qua) {
        selected_checkbox = cb_qua;
        selected_ecc = qrcodegen_Ecc_QUARTILE;
    } else if (sender == cb_high) {
        selected_checkbox = cb_high;
        selected_ecc = qrcodegen_Ecc_HIGH;
    } else {
        return;
    }

    updating_ecc = true;

    // Don't allow the currently selected option to be unchecked.
    Rcheckbox_set_checked(selected_checkbox, true);

    // Uncheck all other options.
    if (selected_checkbox != cb_low)
        Rcheckbox_set_checked(cb_low, false);

    if (selected_checkbox != cb_med)
        Rcheckbox_set_checked(cb_med, false);

    if (selected_checkbox != cb_qua)
        Rcheckbox_set_checked(cb_qua, false);

    if (selected_checkbox != cb_high)
        Rcheckbox_set_checked(cb_high, false);

    current_ecc = selected_ecc;

    updating_ecc = false;
}

int main(void) {
    printf("test\n");
    if (!Rinit(R_BACKEND_VULKAN))
        return 1;

    window_id = Rwindow_create_from_xml(hello_xml, hello_xml_size);
    if (window_id == R_INVALID_ID) {
        fprintf(stderr, "Could not load assets/hello.xml\n");
        Rshutdown();
        return 1;
    }

    input_text      = Rwidget_find(window_id, "input_text");
    cb_low          = Rwidget_find(window_id, "low");
    cb_med          = Rwidget_find(window_id, "med");
    cb_qua          = Rwidget_find(window_id, "qua");
    cb_high         = Rwidget_find(window_id, "high");
    sp_border       = Rwidget_find(window_id, "border");
    sp_scale        = Rwidget_find(window_id, "scale");
    input_light     = Rwidget_find(window_id, "light");
    input_dark      = Rwidget_find(window_id, "dark");
    sp_vermin       = Rwidget_find(window_id, "vermin");
    sp_vermax       = Rwidget_find(window_id, "vermax");
    sp_maskpattern  = Rwidget_find(window_id, "maskpattern");
    cb_boostecc     = Rwidget_find(window_id, "boostecc");
    qr_canvas       = Rwidget_find(window_id, "qr_canvas");
    status_label    = Rwidget_find(window_id, "status");

    if (!widgets_are_valid()) {
        fprintf(stderr, "One or more named widgets could not be found in hello.xml\n");
        Rshutdown();
        return 1;
    }

    Rcustom_set_paint_callback(qr_canvas, paint_qr, NULL);
    Raction_bind("generate_qr", on_generate_action, NULL);
    Raction_bind("save_qr", on_save_qr_action, NULL);
    Raction_bind("on_ecc_toggled", on_ecc_toggled, NULL);
    set_status("Enter text, choose options, and generate a QR code.");

    while (Rwait_frame(-1.0)) {
        /* Raven waits for UI events and redraws invalidated widgets. */
    }

    Rshutdown();
    return 0;
}

