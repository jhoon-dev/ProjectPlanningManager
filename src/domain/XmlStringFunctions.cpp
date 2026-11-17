#include "domain/XmlStringFunctions.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <vector>
namespace jobs {
namespace {
[[noreturn]] void invalid(const std::string &message) {
    throw AppError(ErrorCode::FORMULA_INVALID, {"XML 문자열 함수: " + message});
}
std::vector<std::size_t> offsets(const std::string &s) {
    calculationCheckpoint();
    if (s.size() > 4096)
        invalid("입력과 결과는4,096 bytes까지입니다.");
    std::vector<std::size_t> out{0};
    for (std::size_t i = 0; i < s.size();) {
        calculationCheckpoint();
        const auto first = static_cast<unsigned char>(s[i]);
        const auto n = first < 128                      ? 1u
                       : first >= 0xc2 && first <= 0xdf ? 2u
                       : first >= 0xe0 && first <= 0xef ? 3u
                                                        : 0u;
        if (!n || n > s.size() - i)
            invalid("유효한 UTF-8 BMP 문자만 지원합니다.");
        std::uint32_t cp = first & (n == 1 ? 0x7f : n == 2 ? 0x1f : 0x0f);
        for (std::size_t j = 1; j < n; ++j) {
            const auto c = static_cast<unsigned char>(s[i + j]);
            if ((c & 0xc0) != 0x80)
                invalid("UTF-8 오류");
            cp = (cp << 6) | (c & 0x3f);
        }
        if ((n == 3 && cp < 0x800) || (cp >= 0xd800 && cp <= 0xdfff))
            invalid("비정규 UTF-8 또는서로게이트는지원하지 않습니다.");
        i += n;
        out.push_back(i);
    }
    return out;
}
std::string comparison(std::string s, int compare) {
    if (compare < 0 || compare > 1)
        invalid("비교는0(정확 비교) 또는1(ASCII 대소문자 무시)입니다.");
    if (compare)
        for (auto &c : s) {
            if (static_cast<unsigned char>(c) >= 128)
                invalid("비교1의 검색 문자열은ASCII만 지원합니다. BMP에는비교0을 사용하세요.");
            if (c >= 'A' && c <= 'Z')
                c = char(c + 32);
        }
    return s;
}
void append(std::string &out, const std::string &s, std::size_t at, std::size_t n) {
    if (n > 4096 - out.size())
        invalid("문자열 결과는4,096 bytes까지입니다.");
    out.append(s, at, n);
}
} // namespace
std::string xmlReplace(const std::string &s, const std::string &find, const std::string &replacement,
                       std::int64_t start, std::int64_t count, int compare) {
    if (start < 1 || start > 2147483647 || count < -1 || count > 2147483647)
        invalid("Replace 시작은양수, 횟수는-1 또는0 이상의32비트 정수입니다.");
    const auto positions = offsets(s);
    (void)offsets(find);
    (void)offsets(replacement);
    const auto hay = comparison(s, compare), needle = comparison(find, compare);
    if (std::uint64_t(start) >= positions.size())
        return "";
    auto at = positions[std::size_t(start - 1)];
    if (find.empty() || count == 0)
        return s.substr(at);
    std::string out;
    std::int64_t replaced = 0;
    while (at < s.size() && (count == -1 || replaced < count)) {
        calculationCheckpoint();
        const auto hit = hay.find(needle, at);
        if (hit == std::string::npos)
            break;
        append(out, s, at, hit - at);
        append(out, replacement, 0, replacement.size());
        at = hit + needle.size();
        ++replaced;
    }
    append(out, s, at, s.size() - at);
    return out;
}
std::int64_t xmlInStrRev(const std::string &s, const std::string &find, std::int64_t start, int compare) {
    if (start < -1 || start == 0 || start > 2147483647)
        invalid("InStrRev 시작은-1 또는양수32비트 정수입니다.");
    const auto positions = offsets(s);
    (void)offsets(find);
    const auto hay = comparison(s, compare), needle = comparison(find, compare);
    const auto size = positions.size() - 1;
    const auto end = start == -1 ? size : std::size_t(start);
    if (!size || end > size)
        return 0;
    if (needle.empty())
        return std::int64_t(end);
    const auto endByte = positions[end];
    if (needle.size() > endByte)
        return 0;
    const auto hit = hay.rfind(needle, endByte - needle.size());
    calculationCheckpoint();
    return hit == std::string::npos
               ? 0
               : std::int64_t(std::lower_bound(positions.begin(), positions.end(), hit) - positions.begin()) + 1;
}
std::string xmlStrReverse(const std::string &s) {
    const auto positions = offsets(s);
    std::string out;
    out.reserve(s.size());
    for (auto i = positions.size() - 1; i > 0; --i) {
        calculationCheckpoint();
        out.append(s, positions[i - 1], positions[i] - positions[i - 1]);
    }
    return out;
}
} // namespace jobs
