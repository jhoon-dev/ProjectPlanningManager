#include "domain/XmlUnicode.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
namespace jobs {
namespace {
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(ErrorCode::FORMULA_INVALID, {"XML Unicode 문자 코드: " + s});
}
void permitted(std::uint32_t cp) {
    if ((cp < 32 && cp != 9 && cp != 10 && cp != 13) || cp == 127 || (cp >= 0xd800 && cp <= 0xdfff) || cp >= 0xfffe)
        invalid("native/XML 텍스트에 사용할 수 있는 BMP 문자만 지원합니다.");
}
} // namespace
std::int64_t xmlAscWide(const std::string &s) {
    calculationCheckpoint();
    if (s.empty() || s.size() > 4096)
        invalid("AscW 문자열은 비어 있지 않아야 하며 4,096 bytes까지입니다.");
    std::uint32_t firstCode = 0;
    for (std::size_t i = 0; i < s.size();) {
        calculationCheckpoint();
        const auto first = static_cast<unsigned char>(s[i]);
        const auto n = first < 128                      ? 1u
                       : first >= 0xc2 && first <= 0xdf ? 2u
                       : first >= 0xe0 && first <= 0xef ? 3u
                                                        : 0u;
        if (!n || n > s.size() - i)
            invalid("유효한 UTF-8 BMP 문자열이 필요합니다.");
        std::uint32_t cp = first & (n == 1 ? 0x7f : n == 2 ? 0x1f : 0x0f);
        for (std::size_t j = 1; j < n; ++j) {
            const auto c = static_cast<unsigned char>(s[i + j]);
            if ((c & 0xc0) != 0x80)
                invalid("UTF-8 연속 바이트 오류");
            cp = (cp << 6) | (c & 0x3f);
        }
        if (n == 3 && cp < 0x800)
            invalid("비정규 UTF-8은 지원하지 않습니다.");
        permitted(cp);
        if (!i)
            firstCode = cp;
        i += n;
    }
    return firstCode < 32768 ? std::int64_t(firstCode) : std::int64_t(firstCode) - 65536;
}
std::string xmlChrWide(std::int64_t code) {
    calculationCheckpoint();
    if (code < -32768 || code > 65535)
        invalid("ChrW 코드는 반올림 후 -32768~65535입니다.");
    const auto cp = std::uint32_t(code < 0 ? code + 65536 : code);
    permitted(cp);
    std::string out;
    if (cp < 0x80)
        out += char(cp);
    else if (cp < 0x800) {
        out += char(0xc0 | (cp >> 6));
        out += char(0x80 | (cp & 0x3f));
    } else {
        out += char(0xe0 | (cp >> 12));
        out += char(0x80 | ((cp >> 6) & 0x3f));
        out += char(0x80 | (cp & 0x3f));
    }
    return out;
}
} // namespace jobs
