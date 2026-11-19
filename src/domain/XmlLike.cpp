#include "domain/XmlLike.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <vector>
namespace ppm {
namespace {
using Code = std::uint32_t;
[[noreturn]] void invalid(const std::string &message) {
    throw AppError(ErrorCode::FORMULA_INVALID, {"XML Like: " + message});
}
std::vector<Code> characters(const std::string &s) {
    if (s.size() > 4096)
        invalid("입력과 패턴은4,096 bytes까지입니다.");
    std::vector<Code> out;
    for (std::size_t i = 0; i < s.size();) {
        calculationCheckpoint();
        const auto first = static_cast<unsigned char>(s[i]);
        const auto n = first < 128                      ? 1u
                       : first >= 0xc2 && first <= 0xdf ? 2u
                       : first >= 0xe0 && first <= 0xef ? 3u
                                                        : 0u;
        if (!n || n > s.size() - i)
            invalid("유효한 UTF-8 BMP 문자만 지원합니다.");
        Code cp = first & (n == 1 ? 0x7f : n == 2 ? 0x1f : 0x0f);
        for (std::size_t j = 1; j < n; ++j) {
            const auto c = static_cast<unsigned char>(s[i + j]);
            if ((c & 0xc0) != 0x80)
                invalid("UTF-8 오류");
            cp = (cp << 6) | (c & 0x3f);
        }
        if ((n == 3 && cp < 0x800) || (cp >= 0xd800 && cp <= 0xdfff))
            invalid("비정규 UTF-8 또는서로게이트는지원하지 않습니다.");
        out.push_back(cp);
        i += n;
    }
    return out;
}
struct Token {
    enum Kind { Literal, Any, Digit, Star, List } kind;
    Code character = 0;
    bool negate = false;
    std::vector<std::pair<Code, Code>> ranges;
    bool accepts(Code c) const {
        if (kind == Literal)
            return c == character;
        if (kind == Any)
            return true;
        if (kind == Digit)
            return c >= '0' && c <= '9';
        const auto i =
            std::upper_bound(ranges.begin(), ranges.end(), c, [](Code v, const auto &r) { return v < r.first; });
        const bool hit = i != ranges.begin() && c <= std::prev(i)->second;
        return negate ? !hit : hit;
    }
};
std::vector<Token> compile(const std::vector<Code> &p) {
    std::vector<Token> out;
    for (std::size_t i = 0; i < p.size(); ++i) {
        calculationCheckpoint();
        const auto c = p[i];
        if (c != '[') {
            const auto kind = c == '*' ? Token::Star : c == '?' ? Token::Any : c == '#' ? Token::Digit : Token::Literal;
            if (kind != Token::Star || out.empty() || out.back().kind != Token::Star)
                out.push_back({kind, c, false, {}});
            continue;
        }
        const auto end = std::find(p.begin() + i + 1, p.end(), Code(']'));
        if (end == p.end())
            invalid("닫히지 않은 문자 목록입니다.");
        const auto finish = std::size_t(end - p.begin());
        if (finish == i + 1) { // [] matches the empty string.
            i = finish;
            continue;
        }
        Token token{Token::List, 0, false, {}};
        auto at = i + 1;
        if (p[at] == '!' && at + 1 < finish) {
            token.negate = true;
            ++at;
        }
        while (at < finish) {
            calculationCheckpoint();
            const auto first = p[at++];
            auto last = first;
            if (at + 1 < finish && p[at] == '-') {
                last = p[at + 1];
                at += 2;
                if (last < first)
                    invalid("문자 범위는오름차순이어야 합니다.");
            }
            token.ranges.emplace_back(first, last);
        }
        std::sort(token.ranges.begin(), token.ranges.end());
        std::vector<std::pair<Code, Code>> merged;
        for (const auto &range : token.ranges) {
            if (!merged.empty() && range.first <= merged.back().second + 1)
                merged.back().second = std::max(merged.back().second, range.second);
            else
                merged.push_back(range);
        }
        token.ranges = std::move(merged);
        out.push_back(std::move(token));
        i = finish;
    }
    return out;
}
} // namespace
bool xmlLikeFormula(const XmlFormula &f) {
    bool quote = false, date = false, ref = false;
    const auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; };
    for (std::size_t i = 0; i < f.source.size(); ++i) {
        const auto c = f.source[i];
        if (ref) {
            if (c == '\\' && i + 1 < f.source.size())
                ++i;
            else if (c == ']')
                ref = false;
        } else if (quote) {
            if (c == '"') {
                if (i + 1 < f.source.size() && f.source[i + 1] == '"')
                    ++i;
                else
                    quote = false;
            }
        } else if (date) {
            if (c == '#')
                date = false;
        } else if (c == '"' || c == '#' || c == '[') {
            quote = c == '"';
            date = c == '#';
            ref = c == '[';
        } else if (letter(c)) {
            const auto start = i;
            while (i + 1 < f.source.size() &&
                   (letter(f.source[i + 1]) || (f.source[i + 1] >= '0' && f.source[i + 1] <= '9')))
                ++i;
            auto word = f.source.substr(start, i - start + 1);
            for (auto &v : word)
                if (v >= 'A' && v <= 'Z')
                    v += 'a' - 'A';
            if (word == "like")
                return true;
        }
    }
    return false;
}
bool hasXmlLikeOperator(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlLikeFormula(*f.xmlFormula))
                return true;
    return false;
}
bool xmlLikeMatch(const std::string &text, const std::string &pattern) {
    calculationCheckpoint();
    const auto s = characters(text);
    const auto tokens = compile(characters(pattern));
    const bool star = std::any_of(tokens.begin(), tokens.end(), [](const auto &t) { return t.kind == Token::Star; });
    if (!star) {
        if (s.size() != tokens.size())
            return false;
        for (std::size_t i = 0; i < s.size(); ++i) {
            calculationCheckpoint();
            if (!tokens[i].accepts(s[i]))
                return false;
        }
        return true;
    }
    if (tokens.size() > 2000000 / (s.size() + 1))
        invalid("패턴 비교는최대2,000,000상태까지입니다.");
    std::vector<unsigned char> previous(tokens.size() + 1), current(tokens.size() + 1);
    previous[0] = 1;
    for (std::size_t j = 0; j < tokens.size(); ++j)
        previous[j + 1] = previous[j] && tokens[j].kind == Token::Star;
    for (const auto c : s) {
        calculationCheckpoint();
        current[0] = 0;
        for (std::size_t j = 0; j < tokens.size(); ++j)
            current[j + 1] =
                tokens[j].kind == Token::Star ? current[j] || previous[j + 1] : previous[j] && tokens[j].accepts(c);
        previous.swap(current);
    }
    return previous.back();
}
} // namespace ppm
