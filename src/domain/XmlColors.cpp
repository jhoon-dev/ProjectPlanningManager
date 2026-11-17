#include "domain/XmlColors.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <array>
namespace jobs {
std::int64_t xmlRgbColor(std::int64_t red, std::int64_t green, std::int64_t blue) {
    calculationCheckpoint();
    for (const auto n : {red, green, blue})
        if (n < 0 || n > 32767)
            throw AppError(ErrorCode::FORMULA_INVALID,
                           {"RGB의 각 성분은 반올림 후 0~32767입니다. 255 초과는 255로 제한합니다."});
    return std::min(red, std::int64_t(255)) + std::min(green, std::int64_t(255)) * 256 +
           std::min(blue, std::int64_t(255)) * 65536;
}
std::int64_t xmlQbColor(std::int64_t color) {
    calculationCheckpoint();
    if (color < 0 || color > 15)
        throw AppError(ErrorCode::FORMULA_INVALID, {"QBColor의 색상 번호는 반올림 후 0~15입니다."});
    static constexpr std::array<std::int64_t, 16> palette = {0x000000, 0x800000, 0x008000, 0x808000, 0x000080, 0x800080,
                                                             0x008080, 0xc0c0c0, 0x808080, 0xff0000, 0x00ff00, 0xffff00,
                                                             0x0000ff, 0xff00ff, 0x00ffff, 0xffffff};
    return palette[std::size_t(color)];
}
} // namespace jobs
