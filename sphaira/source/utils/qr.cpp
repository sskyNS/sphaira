#include "utils/qr.hpp"

#include "qrcodegen.h"

namespace sphaira::qr {

bool Generate(const std::string& text, std::vector<uint8_t>& out_rgba, int& out_size, int scale) {
    uint8_t temp[qrcodegen_BUFFER_LEN_MAX];
    uint8_t qrcode[qrcodegen_BUFFER_LEN_MAX];

    if (!qrcodegen_encodeText(text.c_str(), temp, qrcode,
            qrcodegen_Ecc_MEDIUM, 1, 10, qrcodegen_Mask_AUTO, true)) {
        return false;
    }

    const int size = qrcodegen_getSize(qrcode);
    const int border = 4;
    const int dim = (size + border * 2) * scale;

    // 白色背景。
    out_rgba.assign((size_t)dim * dim * 4, 0xFF);

    for (int y = 0; y < size; y++) {
        for (int x = 0; x < size; x++) {
            if (!qrcodegen_getModule(qrcode, x, y)) {
                continue;
            }

            for (int dy = 0; dy < scale; dy++) {
                for (int dx = 0; dx < scale; dx++) {
                    const int px = (x + border) * scale + dx;
                    const int py = (y + border) * scale + dy;
                    const size_t idx = ((size_t)py * dim + px) * 4;
                    out_rgba[idx + 0] = 0x00;
                    out_rgba[idx + 1] = 0x00;
                    out_rgba[idx + 2] = 0x00;
                    out_rgba[idx + 3] = 0xFF;
                }
            }
        }
    }

    out_size = dim;
    return true;
}

} // namespace sphaira::qr
