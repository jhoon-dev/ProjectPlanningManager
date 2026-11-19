#include "domain/XmlCalendar.h"
#include "domain/XmlUnitFields.h"
#include "domain/ResourceAvailability.h"
#include "domain/XmlTimeZone.h"
#include "domain/XmlSchedule.h"
#include "domain/XmlSummaryDuration.h"
#include "domain/XmlBaselines.h"
#include "domain/XmlFormula.h"
#include "domain/XmlFormatting.h"
#include "domain/XmlDateFunctions.h"
#include "domain/XmlNumberText.h"
#include "domain/XmlMath.h"
#include "domain/XmlArithmetic.h"
#include "domain/XmlLogical.h"
#include "domain/XmlLike.h"
#include "domain/XmlUnicode.h"
#include "domain/XmlDateNames.h"
#include "domain/XmlDateDisplay.h"
#include "domain/XmlNumericDisplay.h"
#include "domain/XmlColors.h"
#include "domain/XmlPartition.h"
#include "domain/XmlDepreciation.h"
#include "domain/XmlSingle.h"
#include "domain/XmlProjectConversion.h"
#include "domain/XmlStringFunctions.h"
#include "domain/Classification.h"
#include "domain/FormulaFields.h"
#include "domain/XmlRollDown.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <array>
#include <charconv>
#include <chrono>
#include <map>
#include <set>
#include <variant>
namespace ppm {
namespace {
using N = std::int64_t;
constexpr N scale = 1000000, limit = 9000000000000000000LL;
struct Failure : std::runtime_error {
    std::string message;
    explicit Failure(std::string s) : std::runtime_error(s), message(std::move(s)) {}
};
[[noreturn]] void bad(const std::string &s) {
    throw AppError(ErrorCode::FORMULA_INVALID, {"XML 계산식: " + s});
}
std::string lower(std::string s) {
    for (auto &c : s)
        if (c >= 'A' && c <= 'Z')
            c = char(c + 32);
    return s;
}
std::string referenceKey(const std::string &token) {
    auto folded = lower(token);
    for (const auto &prefix :
         {std::string("field:"), std::string("project:"), std::string("task:"), std::string("resource:")})
        if (folded.starts_with(prefix))
            return prefix + token.substr(prefix.size());
    return folded;
}
std::string trim(std::string s) {
    const auto a = s.find_first_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, s.find_last_not_of(" \t\r\n") - a + 1);
}
void token(std::string &key, const std::string &v) {
    key += std::to_string(v.size()) + ":" + v;
}
std::string owner(FieldScope s, const std::string &key) {
    return std::to_string(int(s)) + ":" + key;
}
N add(N a, N b) {
    if ((b > 0 && a > limit - b) || (b < 0 && a < -limit - b))
        throw Failure{"수치 범위 초과"};
    return a + b;
}
N muldiv(N a, N b, N denominator) {
    if (!denominator)
        throw Failure{"0으로 나눌 수 없습니다."};
    const bool neg = (a < 0) ^ (b < 0) ^ (denominator < 0);
    const auto mag = [](N n) { return std::uint64_t(n < 0 ? -n : n); };
    std::uint64_t low = 0, high = 0, x = mag(a), xh = 0, y = mag(b);
    for (int i = 0; i < 64; ++i) {
        if ((y >> i) & 1) {
            auto old = low;
            low += x;
            high += xh + (low < old);
        }
        xh = (xh << 1) | (x >> 63);
        x <<= 1;
    }
    const auto d = mag(denominator);
    std::uint64_t remainder = 0, q = 0;
    // remainder may approach 9e18; doubling remains within uint64.
    for (int i = 127; i >= 0; --i) {
        remainder = remainder * 2 + (i >= 64 ? (high >> (i - 64)) & 1 : (low >> i) & 1);
        auto digit = remainder >= d;
        if (digit)
            remainder -= d;
        if (q > (std::uint64_t(limit) - digit) / 2)
            throw Failure{"수치 범위 초과"};
        q = q * 2 + digit;
    }
    if (remainder >= d - remainder) {
        if (q == std::uint64_t(limit))
            throw Failure{"수치 범위 초과"};
        ++q;
    }
    return neg ? -N(q) : N(q);
}
N numeric(const std::string &raw) {
    auto v = trim(raw);
    if (v.empty() || v.size() > 32)
        throw Failure{"숫자 변환 실패"};
    std::size_t at = 0;
    bool neg = false;
    if (v[0] == '+' || v[0] == '-') {
        neg = v[0] == '-';
        ++at;
    }
    N whole = 0, fraction = 0, factor = scale;
    auto begin = at;
    while (at < v.size() && v[at] >= '0' && v[at] <= '9') {
        if (whole > (limit / scale - (v[at] - '0')) / 10)
            throw Failure{"숫자 범위 초과"};
        whole = whole * 10 + v[at++] - '0';
    }
    if (at == begin)
        throw Failure{"숫자 변환 실패"};
    if (at < v.size() && v[at] == '.') {
        auto first = ++at;
        while (at < v.size() && v[at] >= '0' && v[at] <= '9' && factor > 1) {
            factor /= 10;
            fraction += (v[at++] - '0') * factor;
        }
        if (at == first)
            throw Failure{"소수점 형식 오류"};
    }
    if (at != v.size())
        throw Failure{"소수 6자리의 점 소수만 지원합니다."};
    auto n = add(whole * scale, fraction);
    return neg ? -n : n;
}
std::string numberText(N n) {
    bool neg = n < 0;
    auto v = std::uint64_t(neg ? -n : n);
    auto out = (neg ? "-" : "") + std::to_string(v / scale);
    if (v % scale) {
        auto f = std::to_string(v % scale + scale).substr(1);
        while (f.back() == '0')
            f.pop_back();
        out += '.' + f;
    }
    return out;
}
struct Ref {
    std::size_t begin, end;
    std::string name;
};
std::vector<Ref> references(const std::string &s) {
    if (s.empty() || s.size() > 2048)
        bad("식은 1~2,048 bytes입니다.");
    std::vector<Ref> out;
    bool quote = false, date = false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        auto c = s[i];
        if ((static_cast<unsigned char>(c) < 32 && c != '\r' && c != '\n' && c != '\t') || c == 127)
            bad("제어문자 오류");
        if (c == '"' && !date) {
            if (quote && i + 1 < s.size() && s[i + 1] == '"') {
                ++i;
                continue;
            }
            quote = !quote;
            continue;
        }
        if (quote)
            continue;
        if (c == '#') {
            date = !date;
            continue;
        }
        if (date || c != '[')
            continue;
        const auto begin = i++;
        std::string name;
        bool closed = false;
        for (; i < s.size(); ++i) {
            if (s[i] == '\\' && i + 1 < s.size() && (s[i + 1] == ']' || s[i + 1] == '\\')) {
                name += s[++i];
                continue;
            }
            if (s[i] == ']') {
                closed = true;
                break;
            }
            name += s[i];
        }
        if (!closed)
            bad("참조 닫는 대괄호 누락");
        name = trim(name);
        if (name.empty() || name.size() > 512 || out.size() >= 128)
            bad("필드 참조 한도/형식 오류");
        out.push_back({begin, i + 1, name});
    }
    if (quote || date)
        bad("문자열/날짜 리터럴 닫기 누락");
    return out;
}
enum class Kind { Null, Number, Text, Boolean, Date };
struct V {
    Kind kind = Kind::Null;
    N n = 0;
    std::string text;
    bool duration = false, elapsed = false, estimated = false;
};
V num(N n) {
    V v;
    v.kind = Kind::Number;
    v.n = n;
    return v;
}
V boolean(bool b) {
    V v;
    v.kind = Kind::Boolean;
    v.n = b ? -scale : 0;
    return v;
}
V text(std::string s) {
    if (s.size() > 4096)
        throw Failure{"문자열 결과는4,096 bytes까지입니다."};
    V v;
    v.kind = Kind::Text;
    v.text = std::move(s);
    return v;
}
V date(const std::string &s) {
    V v;
    v.kind = Kind::Date;
    v.text = normalizeFieldDateTime(s.size() == 10 ? s + "T00:00:00" : s);
    return v;
}
N asNumber(const V &v) {
    if (v.kind == Kind::Number || v.kind == Kind::Boolean)
        return v.n;
    throw Failure{"숫자 형식이 필요합니다."};
}
bool truth(const V &v) {
    return asNumber(v) != 0;
}
std::string asText(const V &v) {
    if (v.kind == Kind::Null)
        return "";
    if (v.kind == Kind::Text || v.kind == Kind::Date)
        return v.text;
    if (v.kind == Kind::Boolean)
        return v.n ? "True" : "False";
    return numberText(v.n);
}
struct Node {
    std::string op, word;
    std::vector<int> args;
    V literal;
};
struct Syntax {
    const std::string &s;
    std::size_t at = 0;
    int depth = 0;
    std::vector<Node> nodes;
    // Existing expressions retain their original Mod/unary grammar. New operators opt into VBA precedence.
    bool pattern = xmlLikeFormula({s, {}, false});
    bool logical = pattern || xmlLogicalFormula({s, {}, false});
    bool arithmetic = logical || xmlArithmeticFormula({s, {}, false});
    void spaces() {
        while (at < s.size() && (s[at] == ' ' || s[at] == '\t' || s[at] == '\r' || s[at] == '\n'))
            ++at;
    }
    bool take(char c) {
        spaces();
        if (at < s.size() && s[at] == c) {
            ++at;
            return true;
        }
        return false;
    }
    int put(Node n) {
        calculationCheckpoint();
        if (nodes.size() >= 128)
            bad("최대128개 값/연산입니다.");
        nodes.push_back(std::move(n));
        return int(nodes.size() - 1);
    }
    int primary() {
        if (++depth > 32)
            bad("문법 중첩은32단계까지입니다.");
        spaces();
        int out = -1;
        if (take('(')) {
            out = expression(0);
            if (!take(')'))
                bad("닫는 괄호 누락");
        } else if (take('+'))
            out = put({"pos", {}, {arithmetic ? expression(logical ? 12 : 10) : primary()}, {}});
        else if (take('-'))
            out = put({"neg", {}, {arithmetic ? expression(logical ? 12 : 10) : primary()}, {}});
        else if (at < s.size() && s[at] == '[') {
            auto all = references(s.substr(at));
            if (all.empty() || all[0].begin != 0)
                bad("참조 오류");
            auto r = all[0];
            at += r.end;
            out = put({"ref", r.name, {}, {}});
        } else if (take('"')) {
            std::string value;
            bool closed = false;
            while (at < s.size()) {
                if (s[at] == '"') {
                    ++at;
                    if (at < s.size() && s[at] == '"') {
                        value += '"';
                        ++at;
                        continue;
                    }
                    closed = true;
                    break;
                }
                value += s[at++];
            }
            if (!closed)
                bad("문자열 닫기 누락");
            out = put({"literal", {}, {}, text(value)});
        } else if (take('#')) {
            auto end = s.find('#', at);
            if (end == std::string::npos)
                bad("날짜 닫기 누락");
            auto v = date(s.substr(at, end - at));
            at = end + 1;
            out = put({"literal", {}, {}, v});
        } else if (at < s.size() && s[at] >= '0' && s[at] <= '9') {
            auto begin = at;
            while (at < s.size() && ((s[at] >= '0' && s[at] <= '9') || s[at] == '.'))
                ++at;
            out = put({"literal", {}, {}, num(numeric(s.substr(begin, at - begin)))});
        } else {
            auto begin = at;
            while (at < s.size() && ((s[at] >= 'A' && s[at] <= 'Z') || (s[at] >= 'a' && s[at] <= 'z') || s[at] == '_'))
                ++at;
            if (begin == at)
                bad("지원하지 않는 값/연산입니다.");
            auto name = lower(s.substr(begin, at - begin));
            if (name == "not")
                out = put({"not", {}, {expression(logical ? 5 : 3)}, {}});
            else if (name == "true" || name == "false" || name == "null")
                out = put({"literal", {}, {}, name == "null" ? V{} : boolean(name == "true")});
            else {
                if (!take('('))
                    bad("미지원 식별자: " + name);
                std::vector<int> args;
                if (!take(')')) {
                    do {
                        if (args.size() >= 16)
                            bad("함수 인수는16개까지입니다.");
                        args.push_back(expression(0));
                        spaces();
                    } while (take(',') || take(';'));
                    if (!take(')'))
                        bad("함수 닫는 괄호 누락");
                }
                out = put({"call", name, std::move(args), {}});
            }
        }
        --depth;
        return out;
    }
    std::pair<std::string, int> operation() {
        spaces();
        if (at == s.size())
            return {{}, -1};
        for (const auto &op :
             std::vector<std::string>{"<=", ">=", "<>", "=", "<", ">", "&", "+", "-", "*", "/", "^", "\\"})
            if (s.compare(at, op.size(), op) == 0)
                return {op, (logical ? 2 : 0) + (op == "^"                ? 11
                                                 : op == "\\"             ? 8
                                                 : op == "*" || op == "/" ? (arithmetic ? 9 : 7)
                                                 : op == "+" || op == "-" ? 6
                                                 : op == "&"              ? 5
                                                                          : 4)};
        auto end = at;
        while (end < s.size() && std::isalpha(static_cast<unsigned char>(s[end])))
            ++end;
        auto word = lower(s.substr(at, end - at));
        // Match the feature scanner's whole identifiers; never execute Eqv0/Imp1 as unversioned operators.
        if ((word == "eqv" || word == "imp" || word == "like") && end < s.size() &&
            ((s[end] >= '0' && s[end] <= '9') || s[end] == '_'))
            return {{}, -1};
        return word == "or"     ? std::pair{word, logical ? 3 : 0}
               : word == "xor"  ? std::pair{word, logical ? 2 : 1}
               : word == "and"  ? std::pair{word, logical ? 4 : 2}
               : word == "eqv"  ? std::pair{word, 1}
               : word == "imp"  ? std::pair{word, 0}
               : word == "like" ? std::pair{word, 6}
               : word == "mod"  ? std::pair{word, logical ? 9 : 7}
                                : std::pair<std::string, int>{{}, -1};
    }
    int expression(int level) {
        auto a = primary();
        while (true) {
            auto [op, p] = operation();
            if (p < level)
                break;
            at += op.size();
            auto b = expression(p + 1);
            a = put({op, {}, {a, b}, {}});
        }
        return a;
    }
    int parse() {
        references(s);
        auto root = expression(0);
        spaces();
        if (at != s.size())
            bad("지원하지 않는 문법 또는 남은 입력");
        return root;
    }
};
const std::set<std::string> additionalFunctions = {"choose", "instr",     "strcomp", "ltrim", "rtrim",
                                                   "space",  "string",    "asc",     "chr",   "cbyte",
                                                   "ccur",   "datevalue", "sqr"};
const std::set<std::string> timeZoneFunctions = {"ppmtimezone", "ppmtimezonetext", "ppmtimezoneoffset", "ppmparseutc"};
const std::set<std::string> formattingFunctions = {"format", "strconv", "ppmformat"};
const std::set<std::string> dateExtractionFunctions = {"datepart", "weekday"};
const std::set<std::string> dateArithmeticFunctions = {"dateadd", "datediff"};
const std::set<std::string> timeFunctions = {"timeserial", "timevalue"};
const std::set<std::string> projectConversionFunctions = {"projdatevalue", "projdateconv", "projdurconv"};
const std::set<std::string> conversionFunctions = {"cdec", "csng", "cvar"};
const std::set<std::string> stringFunctions = {"replace", "instrrev", "strreverse"};
const std::set<std::string> unicodeFunctions = {"ascw", "chrw"};
const std::set<std::string> dateNameFunctions = {"monthname", "weekdayname"};
const std::set<std::string> dateDisplayFunctions = {"formatdatetime"};
const std::set<std::string> numericDisplayFunctions = {"formatnumber", "formatpercent", "formatcurrency"};
const std::set<std::string> colorFunctions = {"rgb", "qbcolor"};
const std::set<std::string> partitionFunctions = {"partition"};
const std::set<std::string> depreciationFunctions = {"sln", "syd"};
const std::set<std::string> mathFunctions = {"atn", "cos", "exp", "log", "sin", "tan"};
const std::set<std::string> numberTextFunctions = {"str", "val", "hex", "oct"};
const std::map<std::string, std::pair<int, int>> functions = {{"iif", {3, 3}},
                                                              {"sln", {3, 3}},
                                                              {"syd", {4, 4}},
                                                              {"partition", {4, 4}},
                                                              {"rgb", {3, 3}},
                                                              {"qbcolor", {1, 1}},
                                                              {"formatnumber", {1, 5}},
                                                              {"formatpercent", {1, 5}},
                                                              {"formatcurrency", {1, 5}},
                                                              {"formatdatetime", {1, 2}},
                                                              {"monthname", {1, 2}},
                                                              {"weekdayname", {1, 3}},
                                                              {"ascw", {1, 1}},
                                                              {"chrw", {1, 1}},
                                                              {"switch", {2, 16}},
                                                              {"abs", {1, 1}},
                                                              {"int", {1, 1}},
                                                              {"fix", {1, 1}},
                                                              {"sgn", {1, 1}},
                                                              {"round", {1, 2}},
                                                              {"min", {2, 16}},
                                                              {"max", {2, 16}},
                                                              {"cstr", {1, 1}},
                                                              {"cdbl", {1, 1}},
                                                              {"cint", {1, 1}},
                                                              {"clng", {1, 1}},
                                                              {"cbool", {1, 1}},
                                                              {"isnull", {1, 1}},
                                                              {"isnumeric", {1, 1}},
                                                              {"len", {1, 1}},
                                                              {"left", {2, 2}},
                                                              {"right", {2, 2}},
                                                              {"mid", {2, 3}},
                                                              {"trim", {1, 1}},
                                                              {"ucase", {1, 1}},
                                                              {"lcase", {1, 1}},
                                                              {"cdate", {1, 1}},
                                                              {"dateserial", {3, 3}},
                                                              {"dateadd", {3, 3}},
                                                              {"datediff", {3, 5}},
                                                              {"datepart", {2, 4}},
                                                              {"weekday", {1, 2}},
                                                              {"timeserial", {3, 3}},
                                                              {"timevalue", {1, 1}},
                                                              {"str", {1, 1}},
                                                              {"val", {1, 1}},
                                                              {"hex", {1, 1}},
                                                              {"oct", {1, 1}},
                                                              {"atn", {1, 1}},
                                                              {"cos", {1, 1}},
                                                              {"exp", {1, 1}},
                                                              {"log", {1, 1}},
                                                              {"sin", {1, 1}},
                                                              {"tan", {1, 1}},
                                                              {"replace", {3, 6}},
                                                              {"instrrev", {2, 4}},
                                                              {"strreverse", {1, 1}},
                                                              {"cdec", {1, 1}},
                                                              {"csng", {1, 1}},
                                                              {"cvar", {1, 1}},
                                                              {"projdatevalue", {1, 1}},
                                                              {"projdateconv", {1, 2}},
                                                              {"projdurconv", {1, 2}},
                                                              {"year", {1, 1}},
                                                              {"month", {1, 1}},
                                                              {"day", {1, 1}},
                                                              {"hour", {1, 1}},
                                                              {"minute", {1, 1}},
                                                              {"second", {1, 1}},
                                                              {"isdate", {1, 1}},
                                                              {"projdurvalue", {1, 1}},
                                                              {"ppmunitavailability", {1, 1}},
                                                              {"ppmresourceload", {1, 1}},
                                                              {"ppmremainingavailability", {1, 1}},
                                                              {"projdateadd", {2, 3}},
                                                              {"projdatesub", {2, 3}},
                                                              {"projdatediff", {2, 3}},
                                                              {"choose", {2, 16}},
                                                              {"instr", {2, 4}},
                                                              {"strcomp", {2, 3}},
                                                              {"ltrim", {1, 1}},
                                                              {"rtrim", {1, 1}},
                                                              {"space", {1, 1}},
                                                              {"string", {2, 2}},
                                                              {"asc", {1, 1}},
                                                              {"chr", {1, 1}},
                                                              {"cbyte", {1, 1}},
                                                              {"ccur", {1, 1}},
                                                              {"datevalue", {1, 1}},
                                                              {"sqr", {1, 1}},
                                                              {"ppmtimezone", {3, 4}},
                                                              {"ppmtimezonetext", {3, 4}},
                                                              {"ppmtimezoneoffset", {2, 3}},
                                                              {"ppmparseutc", {1, 1}},
                                                              {"format", {1, 4}},
                                                              {"ppmformat", {3, 5}},
                                                              {"strconv", {2, 3}}};
struct Compiled {
    std::vector<Node> nodes;
    int root;
    std::map<std::string, XmlFormulaReference> refs;
};
std::shared_ptr<const Compiled> compile(const CustomField &f) {
    const auto key = "xml-syntax/v1/" + xmlFormulaKey(f);
    if (auto c = evaluationCached(key))
        return std::static_pointer_cast<const Compiled>(c);
    auto c = std::make_shared<Compiled>();
    Syntax parser{f.xmlFormula->source, 0, 0, {}};
    c->root = parser.parse();
    c->nodes = std::move(parser.nodes);
    for (const auto &r : f.xmlFormula->references)
        if (!c->refs.emplace(referenceKey(r.token), r).second)
            bad("중복 참조 이름");
    for (const auto &n : c->nodes) {
        if (n.op == "ref" && !c->refs.contains(referenceKey(n.word)))
            bad("미해결 필드 참조: " + n.word);
        if (n.op == "call") {
            auto i = functions.find(n.word);
            if (i == functions.end() || int(n.args.size()) < i->second.first || int(n.args.size()) > i->second.second ||
                (n.word == "switch" && n.args.size() % 2))
                bad("미지원 함수/인수: " + n.word);
        }
    }
    evaluationRemember(key, c);
    return c;
}
std::vector<std::size_t> characters(const std::string &s) {
    std::vector<std::size_t> offsets{0};
    for (std::size_t i = 0; i < s.size();) {
        auto c = static_cast<unsigned char>(s[i]);
        std::size_t length = c < 128 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 : 0;
        if (!length || i + length > s.size())
            throw Failure{"문자열 함수는 BMP 문자만 지원합니다."};
        for (std::size_t j = 1; j < length; ++j)
            if ((static_cast<unsigned char>(s[i + j]) & 0xc0) != 0x80)
                throw Failure{"UTF-8 오류"};
        i += length;
        offsets.push_back(i);
    }
    return offsets;
}
V stamp(std::chrono::sys_time<std::chrono::microseconds> t) {
    if (t < std::chrono::sys_days{std::chrono::year{1} / 1 / 1} ||
        t >= std::chrono::sys_days{std::chrono::year{9999} / 12 / 31} + std::chrono::days{1})
        throw Failure{"날짜 범위 초과"};
    auto day = std::chrono::floor<std::chrono::days>(t);
    std::chrono::year_month_day d{day};
    if (int(d.year()) < 1 || int(d.year()) > 9999)
        throw Failure{"날짜 범위 초과"};
    auto time = t - day;
    auto h = std::chrono::duration_cast<std::chrono::hours>(time);
    time -= h;
    auto m = std::chrono::duration_cast<std::chrono::minutes>(time);
    time -= m;
    auto sec = std::chrono::duration_cast<std::chrono::seconds>(time);
    time -= sec;
    auto pad = [](int n, int w) {
        auto s = std::to_string(n);
        return std::string(std::max(0, w - int(s.size())), '0') + s;
    };
    return date(pad(int(d.year()), 4) + "-" + pad(unsigned(d.month()), 2) + "-" + pad(unsigned(d.day()), 2) + "T" +
                pad(int(h.count()), 2) + ":" + pad(int(m.count()), 2) + ":" + pad(int(sec.count()), 2) +
                (time.count() ? "." + pad(int(time.count()), 6) : ""));
}
N rounded(N n, int places) {
    if (places < 0 || places > 6)
        throw Failure{"Round 자릿수는0~6입니다."};
    N factor = 1;
    for (int i = places; i < 6; ++i)
        factor *= 10;
    N q = n / factor, r = n % factor;
    auto magnitude = r < 0 ? -r : r;
    if (magnitude > factor / 2 || (magnitude == factor / 2 && factor > 1 && (q % 2)))
        q += n < 0 ? -1 : 1;
    return q * factor;
}
V call(const std::string &fn, const std::vector<V> &a) {
    if (depreciationFunctions.contains(fn))
        return num(xmlDepreciation(fn, asNumber(a[0]), asNumber(a[1]), asNumber(a[2]),
                                   a.size() == 4 ? asNumber(a[3]) : scale));
    if (fn == "partition") {
        for (const auto &v : a)
            if (v.kind == Kind::Null)
                return {};
        const auto whole = [](const V &v) { return rounded(asNumber(v), 0) / scale; };
        return text(xmlPartition(whole(a[0]), whole(a[1]), whole(a[2]), whole(a[3])));
    }
    // VBA IIf/Switch and logical operations evaluate all arguments, including unused branches.
    if (fn == "isnull")
        return boolean(a[0].kind == Kind::Null);
    if (fn == "isnumeric") {
        if (a[0].kind == Kind::Number)
            return boolean(true);
        try {
            (void)numeric(asText(a[0]));
            return boolean(a[0].kind == Kind::Text);
        } catch (const Failure &) {
            return boolean(false);
        }
    }
    if (fn == "isdate") {
        try {
            (void)date(asText(a[0]));
            return boolean(true);
        } catch (const AppError &) {
            return boolean(false);
        }
    }
    if (fn == "iif")
        return a[0].kind == Kind::Null ? a[0] : a[truth(a[0]) ? 1 : 2];
    if (fn == "switch") {
        for (std::size_t i = 0; i < a.size(); i += 2)
            if (a[i].kind != Kind::Null && truth(a[i]))
                return a[i + 1];
        return {};
    }
    if (fn == "choose") {
        if (a[0].kind == Kind::Null)
            return {};
        const auto index = rounded(asNumber(a[0]), 0) / scale;
        return index < 1 || std::size_t(index) >= a.size() ? V{} : a[std::size_t(index)];
    }
    const auto integer = [](const V &v, N lo, N hi) {
        const auto n = asNumber(v);
        if (n % scale || n / scale < lo || n / scale > hi)
            throw Failure{"인수의 정수 범위를 확인하세요."};
        return n / scale;
    };
    int firstDay = 1, firstWeek = 1;
    if (fn == "datepart" || fn == "weekday" || fn == "datediff") {
        const auto control = fn == "datepart" ? 2u : fn == "weekday" ? 1u : 3u;
        if (a.size() > control)
            firstDay = int(integer(a[control], 0, 7));
        if (a.size() > control + 1)
            firstWeek = int(integer(a[control + 1], 0, 3));
    }
    if (fn == "format" || fn == "ppmformat") {
        const bool localized = fn == "ppmformat";
        const auto control = localized ? 3u : 2u;
        if (a.size() > 1 && a[1].kind != Kind::Text)
            throw Failure{"Format 표시 형식은 문자열이어야 합니다."};
        const auto lcid = localized ? int(integer(a[2], 1, 65535)) : 1033;
        const auto firstDay = a.size() > control ? int(integer(a[control], 0, 7)) : 1;
        const auto firstWeek = a.size() > control + 1 ? int(integer(a[control + 1], 0, 3)) : 1;
        XmlFormatInput input;
        input.kind = a[0].kind == Kind::Null      ? XmlFormatKind::Null
                     : a[0].kind == Kind::Text    ? XmlFormatKind::Text
                     : a[0].kind == Kind::Date    ? XmlFormatKind::Date
                     : a[0].kind == Kind::Boolean ? XmlFormatKind::Boolean
                                                  : XmlFormatKind::Number;
        input.scaled = a[0].n;
        input.text = a[0].text;
        auto result = xmlFormat(input, a.size() > 1 ? a[1].text : "", lcid, firstDay, firstWeek);
        return result ? text(*result) : V{};
    }
    if (fn == "strconv") {
        const auto flags = int(integer(a[1], 0, 255));
        const auto lcid = a.size() == 3 ? int(integer(a[2], 1, 65535)) : 1033;
        // Validate controls even for Null; an invalid request must not become a missing value.
        (void)xmlStrConv("", flags, lcid);
        if (a[0].kind == Kind::Null)
            return {};
        if (a[0].kind != Kind::Text)
            throw Failure{"StrConv에는 문자열이 필요합니다."};
        return text(xmlStrConv(a[0].text, flags, lcid));
    }
    if (projectConversionFunctions.contains(fn)) {
        const bool duration = fn == "projdurconv";
        const auto format = a.size() > 1 ? int(integer(a[1], 0, 255)) : duration ? 3 : 255;
        if (duration)
            (void)xmlProjectDurationFormat(0, format);
        else if (format > 20 && format != 255)
            throw Failure{"날짜 형식은0~20 또는255입니다."};
        if (a[0].kind == Kind::Null)
            return {};
        if (duration) {
            if (a[0].kind != Kind::Number && a[0].kind != Kind::Text)
                throw Failure{"ProjDurConv에는분 숫자 또는명시 기간 문자열이 필요합니다."};
            const auto minutes = a[0].kind == Kind::Text ? xmlProjectDurationValue(a[0].text) : a[0].n;
            return text(xmlProjectDurationFormat(minutes, format));
        }
        if (a[0].kind != Kind::Date && a[0].kind != Kind::Text)
            throw Failure{"Project 날짜 변환에는ISO 날짜 값 또는문자열이 필요합니다."};
        const auto normalized = xmlProjectDateValue(a[0].text);
        if (!normalized)
            return {};
        return fn == "projdatevalue" ? date(*normalized) : text(xmlProjectDateFormat(*normalized, format));
    }
    if (fn == "replace" || fn == "instrrev" || fn == "strreverse") {
        const bool replace = fn == "replace", reverse = fn == "strreverse";
        const auto control = replace ? 3u : 2u;
        const auto start = !reverse && a.size() > control ? integer(a[control], replace ? 1 : -1, 2147483647)
                           : replace                      ? 1
                                                          : -1;
        if (!reverse && start == 0)
            throw Failure{"InStrRev 시작은-1 또는양수입니다."};
        const auto count = replace && a.size() > 4 ? integer(a[4], -1, 2147483647) : -1;
        const auto compareIndex = replace ? 5u : 3u;
        const auto compare = !reverse && a.size() > compareIndex ? int(integer(a[compareIndex], 0, 1)) : 0;
        const auto strings = reverse ? 1u : replace ? 3u : 2u;
        for (unsigned i = 0; i < strings; ++i)
            if (a[i].kind == Kind::Null) {
                if (fn == "instrrev")
                    return {};
                throw Failure{"Replace/StrReverse의 문자열 인수에는Null을 사용할 수 없습니다."};
            }
        for (unsigned i = 0; i < strings; ++i)
            if (a[i].kind != Kind::Text)
                throw Failure{"Replace/InStrRev/StrReverse에는문자열이 필요합니다. CStr로 명시 변환하세요."};
        if (reverse)
            return text(xmlStrReverse(a[0].text));
        if (replace)
            return text(xmlReplace(a[0].text, a[1].text, a[2].text, start, count, compare));
        return num(xmlInStrRev(a[0].text, a[1].text, start, compare) * scale);
    }
    // Invalid start/compare is an error even when a searched string is Null.
    if (fn == "instr" || fn == "strcomp") {
        const auto strings = fn == "instr" && a.size() > 2 ? 1u : 0u;
        const auto start = strings ? integer(a[0], 1, 2147483647) : 1;
        const auto compareIndex = strings + 2;
        const auto compare = a.size() > compareIndex ? integer(a[compareIndex], 0, 1) : 0;
        if (a[strings].kind == Kind::Null || a[strings + 1].kind == Kind::Null)
            return {};
        if (a[strings].kind != Kind::Text || a[strings + 1].kind != Kind::Text)
            throw Failure{"검색/비교에는 문자열이 필요합니다."};
        auto hay = a[strings].text, needle = a[strings + 1].text;
        const auto offsets = characters(hay);
        (void)characters(needle);
        if (compare) {
            for (const auto *s : {&hay, &needle})
                if (std::any_of(s->begin(), s->end(), [](unsigned char c) { return c >= 128; }))
                    throw Failure{"텍스트 비교1은 ASCII만 지원합니다. BMP의 정확한 비교에는0을 사용하세요."};
            hay = lower(hay);
            needle = lower(needle);
        }
        if (fn == "strcomp")
            return num(hay < needle ? -scale : hay > needle ? scale : 0);
        const auto size = offsets.size() - 1;
        if (std::uint64_t(start) > size)
            return num(0);
        const auto pos = hay.find(needle, offsets[std::size_t(start - 1)]);
        return num(pos == std::string::npos
                       ? 0
                       : N(std::lower_bound(offsets.begin(), offsets.end(), pos) - offsets.begin() + 1) * scale);
    }
    if (colorFunctions.contains(fn)) {
        for (const auto &v : a)
            if (v.kind == Kind::Null)
                throw Failure{"RGB/QBColor의 인수에는 Null을 사용할 수 없습니다."};
        const auto component = [](const V &v) { return rounded(asNumber(v), 0) / scale; };
        return num((fn == "rgb" ? xmlRgbColor(component(a[0]), component(a[1]), component(a[2]))
                                : xmlQbColor(component(a[0]))) *
                   scale);
    }
    if (numericDisplayFunctions.contains(fn)) {
        std::array<N, 4> options = {-1, -2, -2, -2};
        for (std::size_t i = 1; i < a.size(); ++i) {
            options[i - 1] = rounded(asNumber(a[i]), 0) / scale;
            if (options[i - 1] < (i == 1 ? -1 : -2) || options[i - 1] > (i == 1 ? 6 : 0))
                throw Failure{"숫자 표시 옵션의 반올림 후 범위를 확인하세요. 자릿수는 -1/0~6, 나머지는 -2/-1/0입니다."};
        }
        const auto kind = fn == "formatnumber"    ? XmlNumericDisplayKind::Number
                          : fn == "formatpercent" ? XmlNumericDisplayKind::Percent
                                                  : XmlNumericDisplayKind::Currency;
        return text(xmlNumericDisplay(kind, asNumber(a[0]), int(options[0]), int(options[1]), int(options[2]),
                                      int(options[3])));
    }
    if (fn == "formatdatetime") {
        const auto format = a.size() > 1 ? rounded(asNumber(a[1]), 0) / scale : 0;
        if (format < 0 || format > 4)
            throw Failure{"FormatDateTime의 표시 형식은 반올림 후 0~4입니다."};
        if (a[0].kind != Kind::Date && a[0].kind != Kind::Text)
            throw Failure{
                "FormatDateTime에는 날짜 값 또는 명시 ISO 문자열이 필요합니다. Null·숫자는 지원하지 않습니다."};
        return text(xmlFormatDateTime(a[0].text, int(format)));
    }
    if (dateNameFunctions.contains(fn)) {
        for (const auto &value : a)
            if (value.kind == Kind::Null)
                throw Failure{"MonthName/WeekdayName의 인수에는 Null을 사용할 수 없습니다."};
        const auto index = rounded(asNumber(a[0]), 0) / scale;
        const auto first = a.size() > 2 ? rounded(asNumber(a[2]), 0) / scale : 1;
        const bool abbreviate = a.size() > 1 && truth(a[1]);
        const auto max = fn == "monthname" ? 12 : 7;
        if (index < 1 || index > max || first < 0 || first > 7)
            throw Failure{"월·요일 번호와 주 시작일의 반올림 후 범위를 확인하세요."};
        return text(fn == "monthname" ? xmlMonthName(int(index), abbreviate)
                                      : xmlWeekdayName(int(index), abbreviate, int(first)));
    }
    if (unicodeFunctions.contains(fn)) {
        if (a[0].kind == Kind::Null)
            throw Failure{"AscW/ChrW 인수에는 Null을 사용할 수 없습니다."};
        if (fn == "ascw") {
            if (a[0].kind != Kind::Text)
                throw Failure{"AscW에는 문자열이 필요합니다. CStr로 명시 변환하세요."};
            return num(xmlAscWide(a[0].text) * scale);
        }
        return text(xmlChrWide(rounded(asNumber(a[0]), 0) / scale));
    }
    if (auto missing = std::find_if(a.begin(), a.end(), [](const auto &v) { return v.kind == Kind::Null; });
        missing != a.end())
        return *missing;
    if (fn == "cstr")
        return text(asText(a[0]));
    if (fn == "cvar")
        return a[0];
    if (fn == "cdec" || fn == "csng") {
        const auto n = a[0].kind == Kind::Text ? numeric(a[0].text) : asNumber(a[0]);
        return num(fn == "csng" ? xmlSingleNumber(n) : n);
    }
    if (mathFunctions.contains(fn)) {
        const auto n = a[0].kind == Kind::Text ? numeric(a[0].text) : asNumber(a[0]);
        return num(xmlMathNumber(fn, n));
    }
    if (fn == "val") {
        if (a[0].kind != Kind::Text)
            throw Failure{"Val에는문자열이 필요합니다."};
        return num(xmlValNumber(a[0].text));
    }
    if (fn == "str" || fn == "hex" || fn == "oct") {
        const auto n = a[0].kind == Kind::Text ? numeric(a[0].text) : asNumber(a[0]);
        return text(fn == "str" ? xmlStrNumber(n) : xmlRadixNumber(rounded(n, 0) / scale, fn == "hex" ? 16 : 8));
    }
    if (fn == "timeserial") {
        const auto part = [](const V &v) { return rounded(asNumber(v), 0) / scale; };
        return date(xmlTimeSerial(part(a[0]), part(a[1]), part(a[2])));
    }
    if (fn == "timevalue") {
        if (a[0].kind != Kind::Text && a[0].kind != Kind::Date)
            throw Failure{"TimeValue에는시각 문자열 또는날짜 값이 필요합니다."};
        return date(xmlTimeValue(a[0].text));
    }
    if (fn == "datepart" || fn == "weekday") {
        const auto index = fn == "datepart" ? 1u : 0u;
        if (a[index].kind != Kind::Date)
            throw Failure{"DatePart/Weekday에는날짜 값이 필요합니다. ISO 문자열은CDate로 변환하세요."};
        return num(xmlDatePart(fn == "weekday" ? "w" : asText(a[0]), a[index].text, firstDay, firstWeek) * scale);
    }
    if (fn == "ppmparseutc") {
        if (a[0].kind != Kind::Text)
            throw Failure{"PPMParseUtc에는 오프셋 포함 ISO 문자열이 필요합니다."};
        return date(xmlParseUtc(a[0].text));
    }
    if (fn == "ppmtimezone" || fn == "ppmtimezonetext" || fn == "ppmtimezoneoffset") {
        if ((a[0].kind != Kind::Date && a[0].kind != Kind::Text) || a[1].kind != Kind::Text ||
            (fn != "ppmtimezoneoffset" && a[2].kind != Kind::Text))
            throw Failure{"시간대 함수는 ISO 날짜 값과 명시 시간대 문자열을 받습니다."};
        const auto control = fn == "ppmtimezoneoffset" ? 2u : 3u;
        if (a.size() > control && a[control].kind != Kind::Text)
            throw Failure{"중복 시각 정책은 문자열 reject/earlier/later입니다."};
        const auto policy = a.size() > control ? a[control].text : "reject";
        if (fn == "ppmtimezoneoffset")
            return num(N(xmlTimeZoneOffset(asText(a[0]), a[1].text, policy)) * scale);
        const auto converted = fn == "ppmtimezone" ? xmlTimeZoneConvert(asText(a[0]), a[1].text, a[2].text, policy)
                                                   : xmlTimeZoneText(asText(a[0]), a[1].text, a[2].text, policy);
        return fn == "ppmtimezone" ? date(converted) : text(converted);
    }
    if (fn == "cdbl" || fn == "cint" || fn == "clng") {
        auto n = a[0].kind == Kind::Text ? numeric(a[0].text) : asNumber(a[0]);
        auto result = fn == "cdbl" ? n : rounded(n, 0);
        if ((fn == "cint" && (result / scale < -32768 || result / scale > 32767)) ||
            (fn == "clng" && (result / scale < -2147483648LL || result / scale > 2147483647LL)))
            throw Failure{"정수 변환 범위 초과"};
        return num(result);
    }
    if (fn == "cbool")
        return boolean(truth(a[0]));
    if (fn == "cbyte" || fn == "ccur") {
        const auto n = a[0].kind == Kind::Text ? numeric(a[0].text) : asNumber(a[0]);
        const auto value = rounded(n, fn == "cbyte" ? 0 : 4);
        if (fn == "cbyte" && (value < 0 || value > 255 * scale))
            throw Failure{"CByte 결과는0~255입니다."};
        return num(value);
    }
    if (fn == "sqr") {
        const auto x = asNumber(a[0]);
        if (x < 0)
            throw Failure{"Sqr에는 음수를 사용할 수 없습니다."};
        // Compare y*y + extra with x*1e6 using base1000 pieces; no floating point or overflow.
        const auto within = [&](N y, N extra = 0) {
            const auto q = y / 1000, r = y % 1000;
            const auto tail = 2 * q * r * 1000 + r * r + extra;
            const auto whole = q * q + tail / scale;
            return whole < x || (whole == x && tail % scale == 0);
        };
        N lo = 0, hi = 3000000000000LL;
        while (lo < hi) {
            const auto mid = lo + (hi - lo + 1) / 2;
            if (within(mid))
                lo = mid;
            else
                hi = mid - 1;
        }
        return num(lo + (within(lo, lo + 1) ? 1 : 0));
    }
    if (fn == "datevalue") {
        const auto d = a[0].kind == Kind::Date ? a[0] : date(asText(a[0]));
        return date(d.text.substr(0, 10));
    }
    if (fn == "space" || fn == "string" || fn == "chr") {
        const auto code = [&](const V &v) { return std::string(1, char(integer(v, 32, 126))); };
        if (fn == "chr")
            return text(code(a[0]));
        const auto count = std::size_t(integer(a[0], 0, 4096));
        std::string piece = " ";
        if (fn == "string") {
            if (a[1].kind == Kind::Text) {
                const auto offsets = characters(a[1].text);
                if (offsets.size() < 2)
                    throw Failure{"String의 반복 문자는 비어 있을 수 없습니다."};
                piece = a[1].text.substr(0, offsets[1]);
            } else
                piece = code(a[1]);
        }
        if (count > 4096 / piece.size())
            throw Failure{"문자열 결과는4,096 bytes까지입니다."};
        std::string out;
        out.reserve(count * piece.size());
        for (std::size_t i = 0; i < count; ++i)
            out += piece;
        return text(std::move(out));
    }
    if (fn == "asc") {
        if (a[0].kind != Kind::Text || a[0].text.empty() || a[0].text[0] < 32 || a[0].text[0] > 126)
            throw Failure{"Asc/Chr의 문자 코드는 표시 가능한 ASCII32~126만 지원합니다."};
        return num(N(a[0].text[0]) * scale);
    }
    if (fn == "abs" || fn == "int" || fn == "fix" || fn == "sgn" || fn == "round") {
        auto n = asNumber(a[0]);
        if (fn == "abs")
            return num(n < 0 ? -n : n);
        if (fn == "int")
            return num((n / scale - (n < 0 && n % scale ? 1 : 0)) * scale);
        if (fn == "fix")
            return num(n / scale * scale);
        if (fn == "sgn")
            return num(n < 0 ? -scale : n > 0 ? scale : 0);
        auto places = a.size() == 1 ? 0 : asNumber(a[1]);
        if (a.size() > 1 && places % scale)
            throw Failure{"Round 자릿수는 정수입니다."};
        return num(rounded(n, a.size() == 1 ? 0 : int(places / scale)));
    }
    if (fn == "min" || fn == "max") {
        auto n = asNumber(a[0]);
        for (const auto &v : a)
            n = fn == "min" ? std::min(n, asNumber(v)) : std::max(n, asNumber(v));
        return num(n);
    }
    if (fn == "cdate")
        return date(asText(a[0]));
    if (fn == "dateserial") {
        for (const auto &v : a)
            if (asNumber(v) % scale)
                throw Failure{"DateSerial은 정수 인수가 필요합니다."};
        auto y = asNumber(a[0]) / scale, m = asNumber(a[1]) / scale, d = asNumber(a[2]) / scale;
        if (y < 100 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31)
            throw Failure{"DateSerial의 유효한 연/월/일 범위를 확인하세요."};
        auto day = std::chrono::year{int(y)} / unsigned(m) / unsigned(d);
        if (!day.ok())
            throw Failure{"존재하지 않는 날짜"};
        return stamp(std::chrono::time_point_cast<std::chrono::microseconds>(std::chrono::sys_days{day}));
    }
    if (fn == "dateadd" || fn == "datediff") {
        const auto interval = asText(a[0]);
        if (fn == "dateadd") {
            const auto count = integer(a[1], -10000000, 10000000);
            if (a[2].kind != Kind::Date)
                throw Failure{"DateAdd에는날짜 값이 필요합니다."};
            return date(xmlDateAdd(interval, count, a[2].text));
        }
        if (a[1].kind != Kind::Date || a[2].kind != Kind::Date)
            throw Failure{"DateDiff에는두 날짜 값이 필요합니다."};
        return num(xmlDateDifference(interval, a[1].text, a[2].text, firstDay, firstWeek) * scale);
    }
    if (fn == "year" || fn == "month" || fn == "day" || fn == "hour" || fn == "minute" || fn == "second") {
        if (a[0].kind != Kind::Date)
            throw Failure{"날짜 형식이 필요합니다."};
        auto s = a[0].text;
        return num(N(std::stoi(s.substr(fn == "year"     ? 0
                                        : fn == "month"  ? 5
                                        : fn == "day"    ? 8
                                        : fn == "hour"   ? 11
                                        : fn == "minute" ? 14
                                                         : 17,
                                        fn == "year" ? 4 : 2))) *
                   scale);
    }
    if (fn == "projdurvalue") {
        if (a[0].kind == Kind::Number)
            return a[0];
        auto d = parseFieldDuration(asText(a[0]));
        V v = num(d.minutes);
        v.duration = true;
        v.elapsed = d.elapsed;
        v.estimated = d.estimated;
        return v;
    }
    if (a[0].kind != Kind::Text)
        throw Failure{"문자열 형식이 필요합니다."};
    auto s = a[0].text;
    if (fn == "trim" || fn == "ltrim" || fn == "rtrim") {
        const auto begin = s.find_first_not_of(' ');
        if (begin == std::string::npos)
            return text("");
        return text(fn == "ltrim"   ? s.substr(begin)
                    : fn == "rtrim" ? s.substr(0, s.find_last_not_of(' ') + 1)
                                    : s.substr(begin, s.find_last_not_of(' ') - begin + 1));
    }
    if (fn == "ucase" || fn == "lcase") {
        for (char &c : s) {
            if (static_cast<unsigned char>(c) >= 128)
                throw Failure{"대소문자 변환은 ASCII만 지원합니다."};
            if (fn == "ucase" && c >= 'a' && c <= 'z')
                c = char(c - 32);
            if (fn == "lcase" && c >= 'A' && c <= 'Z')
                c = char(c + 32);
        }
        return text(s);
    }
    const auto offsets = characters(s);
    auto size = offsets.size() - 1;
    if (fn == "len")
        return num(N(size) * scale);
    auto lengthInteger = [](const V &v) {
        auto n = asNumber(v);
        if (n % scale || n < 0 || n / scale > 4096)
            throw Failure{"문자열 위치/길이는0~4096 정수입니다."};
        return std::size_t(n / scale);
    };
    auto start = std::size_t(0), length = lengthInteger(a[1]);
    if (fn == "right")
        start = length < size ? size - length : 0;
    else if (fn == "mid") {
        if (!length)
            throw Failure{"Mid 시작 위치는1 이상입니다."};
        start = std::min(size, length - 1);
        length = a.size() == 3 ? lengthInteger(a[2]) : size;
    }
    length = std::min(length, size - start);
    return text(s.substr(offsets[start], offsets[start + length] - offsets[start]));
}
V scalar(const PlanInput &p, const CustomField &f, const std::optional<std::string> &raw) {
    if (!raw)
        return {};
    if (f.type == CustomFieldType::Text)
        return text(*raw);
    if (f.type == CustomFieldType::Flag)
        return boolean(*raw == "1");
    if (dateTimeCustomField(f.type))
        return date(*raw);
    if (f.type == CustomFieldType::Choice)
        throw Failure{"목록 값 참조는 지원하지 않습니다."};
    if (f.type == CustomFieldType::Duration) {
        auto d = parseFieldDuration(*raw);
        auto v = num(d.minutes);
        v.duration = true;
        v.elapsed = d.elapsed;
        v.estimated = d.estimated;
        return v;
    }
    if (f.type == CustomFieldType::Cost) {
        auto minor = parseFieldCost(*raw, p.currency);
        return num(muldiv(minor, currencyDecimals(p.currency) ? 10000 : 1000000, 1));
    }
    return num(parseFieldNumber(*raw));
}
std::optional<std::string> output(const PlanInput &p, const CustomField &f, const V &v) {
    if (v.kind == Kind::Null)
        return {};
    if (f.type == CustomFieldType::Text)
        return normalizeLookupScalar(f.type, asText(v), p.currency);
    if (f.type == CustomFieldType::Flag)
        return truth(v) ? "1" : "0";
    if (dateTimeCustomField(f.type)) {
        if (v.kind != Kind::Date)
            throw Failure{"날짜 결과가 필요합니다."};
        return normalizeLookupScalar(f.type, v.text, p.currency);
    }
    auto n = asNumber(v);
    if (f.type == CustomFieldType::Number)
        return normalizeLookupScalar(f.type, numberText(n), p.currency);
    if (f.type == CustomFieldType::Cost) {
        auto minor = muldiv(n, currencyDecimals(p.currency) ? 100 : 1, scale);
        return formatFieldCost(minor, p.currency);
    }
    if (f.type == CustomFieldType::Duration)
        return normalizeLookupScalar(f.type, formatFieldDuration({n, v.elapsed, v.estimated}), p.currency);
    throw Failure{"목록 결과는 지원하지 않습니다."};
}
struct Context {
    const PlanInput &p;
    const Task *task;
    const Resource *resource;
    const Assignment *assignment;
    const XmlSummaryResolver &summary;
    FieldScope summaryScope;
    std::map<std::string, V> memo;
    std::set<std::string> active;
    std::size_t steps = 0;
    V builtin(const std::string &key, FieldScope scope) {
        if (xmlUnitReference(key)) {
            if (summary && scope == summaryScope)
                return {};
            std::optional<int> units;
            if (key == "builtin:units" && scope == FieldScope::Assignment && assignment)
                units = assignment->units;
            if (key == "builtin:maxunits" && scope == FieldScope::Resource && resource)
                units = resource->maxUnits;
            if (!units)
                return {};
            if (*units < 0 || *units > 100000)
                throw Failure{"배정률/가용률 범위 오류"};
            return num(N(*units) * 10000); // One unit is 100%; native integers are percentage points.
        }
        const auto baseKey = xmlBaselineBaseKey(key);
        auto k = key.substr(8);
        if (xmlComputedReference(key) || xmlActualReference(key) || xmlBaselineReference(key)) {
            auto value = summary && scope == summaryScope
                             ? summary(key)
                             : xmlScheduleReference(p, scope, key, task, resource, assignment);
            if (!value.value) {
                if (!value.missing)
                    throw Failure{value.issue};
                V missing;
                missing.text = value.issue;
                return missing;
            }
            if (xmlActualQuantityReference(key) || xmlRemainingReference(key) || xmlBaselineQuantityReference(key)) {
                auto v =
                    num(baseKey == "builtin:baselinecost" ? numeric(*value.value) : parseFieldNumber(*value.value));
                v.duration = key == "builtin:actualduration" || key == "builtin:remainingduration" ||
                             baseKey == "builtin:baselineduration";
                if (v.duration && baseKey == "builtin:baselineduration" && !(summary && scope == summaryScope)) {
                    auto baseline = XmlScheduleScope::current(p);
                    if (baseline)
                        if (auto slot = xmlBaselineSlot(key)) {
                            auto it = baseline->numberedBaselines.find(*slot);
                            baseline = it == baseline->numberedBaselines.end() ? nullptr : it->second;
                        }
                    if (baseline && baseline->externalBaseline) {
                        const auto row = baseline->externalBaselineRows.find(xmlBaselineRowKey(
                            scope == FieldScope::Project ? "project" : "task", scope == FieldScope::Project ? ""
                                                                               : task                       ? task->id
                                                                                                            : ""));
                        if (row != baseline->externalBaselineRows.end()) {
                            const auto &flags = row->second.flags;
                            int format = flags.contains("DurationFormat") ? std::stoi(flags.at("DurationFormat")) : 3;
                            v.estimated =
                                (flags.contains("EstimatedDuration") && flags.at("EstimatedDuration") == "1") ||
                                format >= 32;
                            if (format >= 32)
                                format -= 32;
                            v.elapsed = format == 4 || format == 6 || format == 8 || format == 10 || format == 12 ||
                                        format == 20;
                        }
                    }
                }
                return v;
            }
            return date(*value.value);
        }
        if (k == "summary")
            return boolean(bool(summary));
        if (k == "name" && summary && scope == summaryScope)
            return {};
        if (k == "name")
            return text(scope == FieldScope::Task && task           ? task->name
                        : scope == FieldScope::Resource && resource ? resource->name
                        : scope == FieldScope::Project              ? p.name
                                                                    : "");
        if (k == "projectstart")
            return date(p.startDate);
        if (scope == FieldScope::Task && task && k == "duration") {
            if (!task->durationMinutes)
                return {};
            auto v = num(muldiv(*task->durationMinutes, scale, 1));
            v.duration = true;
            return v;
        }
        if (scope == FieldScope::Task && task && k == "work") {
            if (!task->workUnitMinutes)
                return {};
            return num(muldiv(*task->workUnitMinutes, scale, 100));
        }
        return {};
    }
    V field(FieldScope scope, const std::string &key) {
        XmlFormulaFrame frame;
        calculationCheckpoint();
        if (++steps > 32768)
            throw Failure{"평가 연산 한도 초과"};
        auto id = owner(scope, key);
        if (active.size() > 64 || !active.insert(id).second)
            throw Failure{"순환/참조 깊이 초과"};
        struct Pop {
            std::set<std::string> &s;
            std::string k;
            ~Pop() {
                s.erase(k);
            }
        } pop{active, id};
        if (memo.contains(id))
            return memo.at(id);
        if (key.starts_with("builtin:"))
            return builtin(key, scope);
        const auto *f = customField(p, key, scope);
        if (!f)
            throw Failure{"참조 필드가 없습니다."};
        V value;
        if (summary && scope == summaryScope) {
            auto r = summary(key);
            if (!r.issue.empty() && !r.missing)
                throw Failure{r.issue};
            value = scalar(p, *f, r.value);
            if (!r.value)
                value.text = r.issue;
        } else if (f->xmlFormula && f->xmlFormula->enabled) {
            auto program = compile(*f);
            value = node(*program, program->root, scope);
            if (value.kind != Kind::Null)
                value = scalar(p, *f, output(p, *f, value));
        } else if (!f->expression.empty()) {
            auto v = nativeFormulaValue(p, scope, key, scope == FieldScope::Task ? task : nullptr,
                                        scope == FieldScope::Resource ? resource : nullptr);
            if (!v.number && !v.missing)
                throw Failure{v.issue};
            value = scalar(p, *f, formulaRawValue(v));
            if (!formulaRawValue(v))
                value.text = v.issue;
        } else {
            const std::vector<TaskAttribute> *attrs =
                scope == FieldScope::Project                    ? &p.projectAttributes
                : scope == FieldScope::Task && task             ? &task->attributes
                : scope == FieldScope::Resource && resource     ? &resource->attributes
                : scope == FieldScope::Assignment && assignment ? &assignment->attributes
                                                                : nullptr;
            auto raw = attrs ? attributeValue(*attrs, key) : std::nullopt;
            if (scope == FieldScope::Assignment && assignment)
                raw = RollDownValues(p).value(*assignment, key);
            value = scalar(p, *f, raw);
        }
        memo.emplace(id, value);
        return value;
    }
    V node(const Compiled &c, int index, FieldScope fieldScope) {
        XmlFormulaFrame frame;
        calculationCheckpoint();
        if (++steps > 32768)
            throw Failure{"평가 연산 한도 초과"};
        const auto &n = c.nodes.at(std::size_t(index));
        if (n.op == "literal")
            return n.literal;
        if (n.op == "ref") {
            const auto &r = c.refs.at(referenceKey(n.word));
            return field(r.scope, r.key);
        }
        std::vector<V> values;
        for (auto child : n.args)
            values.push_back(node(c, child, fieldScope));
        if (n.op == "call" &&
            (n.word == "ppmunitavailability" || n.word == "ppmresourceload" || n.word == "ppmremainingavailability")) {
            if (values[0].kind == Kind::Null)
                return values[0];
            if (values[0].kind != Kind::Date && values[0].kind != Kind::Text)
                throw Failure{"가용량/부하 조회는 명시 ISO 날짜/일시를 받습니다."};
            auto at = date(asText(values[0])).text.substr(0, 16);
            at[10] = ' ';
            if ((summary && fieldScope == summaryScope) || !resource ||
                (fieldScope != FieldScope::Resource && fieldScope != FieldScope::Assignment)) {
                V missing;
                missing.text = "날짜별 가용률은 단일 자원/배정 범위에서 조회하세요.";
                return missing;
            }
            if (n.word != "ppmunitavailability") {
                auto scope = XmlScheduleScope::current(p);
                if (!scope || !scope->resourceLoad) {
                    const auto issue = scope ? scope->resourceLoadIssue : "현재 입력의 일정 계산이 필요합니다.";
                    if (scope && !scope->resourceLoadMissing)
                        throw Failure{issue};
                    V missing;
                    missing.text = issue;
                    return missing;
                }
                auto value = scope->resourceLoad(resource->id, at, n.word == "ppmremainingavailability");
                if (!value.units) {
                    if (!value.missing)
                        throw Failure{value.issue};
                    V missing;
                    missing.text = value.issue;
                    return missing;
                }
                return num(N(*value.units) * 10000);
            }
            const auto units = resourceUnitsAt(*resource, at);
            if (!units) {
                V missing;
                missing.text = "지정 날짜의 자원 가용률 미설정";
                return missing;
            }
            return num(N(*units) * 10000);
        }
        if (n.op == "call" && (n.word == "projdateadd" || n.word == "projdatesub" || n.word == "projdatediff")) {
            if (auto missing =
                    std::find_if(values.begin(), values.end(), [](const auto &v) { return v.kind == Kind::Null; });
                missing != values.end())
                return *missing;
            const auto from = date(asText(values[0]));
            if (values.size() == 3 && values[2].kind != Kind::Text)
                throw Failure{"달력 이름은 문자열이어야 합니다."};
            const auto calendar = values.size() == 3 ? values[2].text : "";
            if (n.word == "projdatediff")
                return num(xmlCalendarDifference(p, fieldScope, task, resource, calendar, from.text,
                                                 date(asText(values[1])).text));
            auto duration = values[1];
            bool elapsed = duration.elapsed;
            N minutes;
            if (duration.kind == Kind::Number)
                minutes = duration.n;
            else if (duration.kind == Kind::Text) {
                auto value = lower(trim(duration.text));
                if (!value.empty() && value.back() == '?')
                    value = trim(value.substr(0, value.size() - 1));
                N factor = 1;
                if (!value.empty() &&
                    (value.back() == 'm' || value.back() == 'h' || value.back() == 'd' || value.back() == 'w')) {
                    auto unit = value.back();
                    value.pop_back();
                    if (!value.empty() && value.back() == 'e') {
                        elapsed = true;
                        value.pop_back();
                    }
                    factor = unit == 'h'   ? 60
                             : unit == 'd' ? (elapsed ? 1440 : 480)
                             : unit == 'w' ? (elapsed ? 10080 : 2400)
                                           : 1;
                }
                minutes = muldiv(numeric(value), factor, 1);
            } else
                throw Failure{"기간은 분 숫자 또는 m/h/d/w 기간 문자열이어야 합니다."};
            if (n.word == "projdatesub")
                minutes = -minutes;
            return date(xmlCalendarShift(p, fieldScope, task, resource, calendar, from.text, minutes, elapsed));
        }
        if (n.op == "call")
            return call(n.word, values);
        const auto &a = values[0];
        if (n.op == "&") {
            if (a.kind == Kind::Null && values[1].kind == Kind::Null)
                return {};
            return text(asText(a) + asText(values[1]));
        }
        if (values.size() == 2 && (n.op == "and" || n.op == "or") &&
            (a.kind == Kind::Null || values[1].kind == Kind::Null)) {
            const auto &known = a.kind == Kind::Null ? values[1] : a;
            if (known.kind != Kind::Null && ((n.op == "and" && !truth(known)) || (n.op == "or" && truth(known))))
                return boolean(n.op == "or");
            return {};
        }
        if (n.op == "imp" && (a.kind == Kind::Null || values[1].kind == Kind::Null)) {
            const auto &b = values[1];
            if (a.kind == Kind::Null && b.kind == Kind::Null)
                return a;
            if (b.kind == Kind::Null) {
                if (a.kind == Kind::Boolean)
                    return truth(a) ? b : boolean(true);
                const auto n = xmlLogicalInteger(asNumber(a));
                return n == -1 ? b : num(xmlLogicalNumber("imp", N(n) * scale, 0));
            }
            if (b.kind == Kind::Boolean)
                return truth(b) ? boolean(true) : a;
            const auto n = xmlLogicalInteger(asNumber(b));
            return n == 0 ? a : num(N(n) * scale);
        }
        if (a.kind == Kind::Null || (values.size() == 2 && values[1].kind == Kind::Null))
            return a.kind == Kind::Null ? a : values[1];
        if (n.op == "pos")
            return num(asNumber(a));
        if (n.op == "neg") {
            auto v = a;
            v.kind = Kind::Number;
            v.n = -asNumber(a);
            return v;
        }
        if (n.op == "not") {
            if (a.kind == Kind::Boolean)
                return boolean(!truth(a));
            auto n = asNumber(a);
            if (n % scale || n / scale < -2147483648LL || n / scale > 2147483647LL)
                throw Failure{"비트 연산은32비트 정수만 지원합니다."};
            return num(N(~std::int32_t(n / scale)) * scale);
        }
        const auto &b = values[1];
        if (n.op == "like") {
            if (a.kind != Kind::Text || b.kind != Kind::Text)
                throw Failure{"Like는문자열 피연산자만 지원합니다. CStr 등으로 명시 변환하세요."};
            return boolean(xmlLikeMatch(a.text, b.text));
        }
        if (n.op == "eqv" || n.op == "imp") {
            if (a.kind == Kind::Boolean && b.kind == Kind::Boolean) {
                const auto x = truth(a), y = truth(b);
                return boolean(n.op == "eqv" ? x == y : !x || y);
            }
            return num(xmlLogicalNumber(n.op, asNumber(a), asNumber(b)));
        }
        if (n.op == "and" || n.op == "or" || n.op == "xor") {
            if (a.kind == Kind::Boolean && b.kind == Kind::Boolean) {
                auto x = truth(a), y = truth(b);
                return boolean(n.op == "and" ? x && y : n.op == "or" ? x || y : x != y);
            }
            auto integer = [](const V &v) {
                auto n = asNumber(v);
                if (n % scale || n / scale < -2147483648LL || n / scale > 2147483647LL)
                    throw Failure{"비트 연산은32비트 정수만 지원합니다."};
                return std::int32_t(n / scale);
            };
            auto x = integer(a), y = integer(b);
            return num(N(n.op == "and" ? (x & y) : n.op == "or" ? (x | y) : (x ^ y)) * scale);
        }
        if (n.op == "=" || n.op == "<>" || n.op == "<" || n.op == ">" || n.op == "<=" || n.op == ">=") {
            int comparison = 0;
            if ((a.kind == Kind::Date && b.kind == Kind::Date) || (a.kind == Kind::Text && b.kind == Kind::Text))
                comparison = a.text < b.text ? -1 : a.text > b.text ? 1 : 0;
            else {
                auto x = asNumber(a), y = asNumber(b);
                comparison = x < y ? -1 : x > y ? 1 : 0;
            }
            return boolean(n.op == "="    ? comparison == 0
                           : n.op == "<>" ? comparison != 0
                           : n.op == "<"  ? comparison < 0
                           : n.op == ">"  ? comparison > 0
                           : n.op == "<=" ? comparison <= 0
                                          : comparison >= 0);
        }
        auto x = asNumber(a), y = asNumber(b);
        if (n.op == "^" || n.op == "\\")
            return num(xmlArithmeticNumber(n.op, x, y));
        V v = num(n.op == "+"   ? add(x, y)
                  : n.op == "-" ? add(x, -y)
                  : n.op == "*" ? muldiv(x, y, scale)
                  : n.op == "/" ? muldiv(x, scale, y)
                                : 0);
        if (n.op == "mod") {
            if (!y)
                throw Failure{"0으로 나눌 수 없습니다."};
            if (x % scale || y % scale)
                throw Failure{"Mod는 정수만 지원합니다."};
            v.n = x % y;
        }
        if (a.duration || b.duration) {
            if (a.duration && b.duration && a.elapsed != b.elapsed)
                throw Failure{"근무/경과 기간 혼합"};
            v.duration = true;
            v.elapsed = a.duration ? a.elapsed : b.elapsed;
            v.estimated = a.estimated || b.estimated;
        }
        return v;
    }
};
bool permitted(FieldScope from, FieldScope to) {
    return from == to || to == FieldScope::Project ||
           (from == FieldScope::Assignment && (to == FieldScope::Task || to == FieldScope::Resource));
}
} // namespace
namespace {
thread_local int xmlFormulaFrames = 0;
}
XmlFormulaFrame::XmlFormulaFrame() {
    if (xmlFormulaFrames >= 128)
        bad("native/XML 통합 평가 깊이는128프레임까지입니다.");
    ++xmlFormulaFrames;
}
XmlFormulaFrame::~XmlFormulaFrame() {
    --xmlFormulaFrames;
}
bool hasXmlFormulas(const PlanInput &p) {
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, s))
            if (f.xmlFormula)
                return true;
    return false;
}
namespace {
bool callsFunctions(const XmlFormula &f, const std::set<std::string> &names) {
    bool quoted = false, literalDate = false, ref = false;
    for (std::size_t i = 0; i < f.source.size(); ++i) {
        const auto c = f.source[i];
        if (ref) {
            if (c == '\\' && i + 1 < f.source.size())
                ++i;
            else if (c == ']')
                ref = false;
            continue;
        }
        if (quoted) {
            if (c == '"') {
                if (i + 1 < f.source.size() && f.source[i + 1] == '"')
                    ++i;
                else
                    quoted = false;
            }
            continue;
        }
        if (literalDate) {
            if (c == '#')
                literalDate = false;
            continue;
        }
        if (c == '"' || c == '#' || c == '[') {
            quoted = c == '"';
            literalDate = c == '#';
            ref = c == '[';
            continue;
        }
        const auto letter = [](char v) { return (v >= 'a' && v <= 'z') || (v >= 'A' && v <= 'Z') || v == '_'; };
        if (letter(c)) {
            const auto begin = i;
            while (i + 1 < f.source.size() &&
                   (letter(f.source[i + 1]) || (f.source[i + 1] >= '0' && f.source[i + 1] <= '9')))
                ++i;
            auto at = i + 1;
            while (at < f.source.size() &&
                   (f.source[at] == ' ' || f.source[at] == '\t' || f.source[at] == '\r' || f.source[at] == '\n'))
                ++at;
            if (names.contains(lower(f.source.substr(begin, i - begin + 1))) && at < f.source.size() &&
                f.source[at] == '(')
                return true;
        }
    }
    return false;
}
} // namespace
bool xmlAdditionalFormula(const XmlFormula &f) {
    return callsFunctions(f, additionalFunctions);
}
bool xmlAvailabilityFormula(const XmlFormula &f) {
    return callsFunctions(f, {"ppmunitavailability", "ppmresourceload", "ppmremainingavailability"});
}
bool xmlResourceLoadFormula(const XmlFormula &f) {
    return callsFunctions(f, {"ppmresourceload", "ppmremainingavailability"});
}
bool hasXmlResourceLoadFunctions(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlResourceLoadFormula(*f.xmlFormula))
                return true;
    return false;
}
bool hasXmlUnitReferences(const PlanInput &p) {
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, s))
            if (f.xmlFormula)
                for (const auto &r : f.xmlFormula->references)
                    if (xmlUnitReference(r.key))
                        return true;
    return false;
}
bool hasXmlAvailabilityFunctions(const PlanInput &p, bool activeOnly) {
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, s))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlAvailabilityFormula(*f.xmlFormula))
                return true;
    return false;
}
bool xmlTimeZoneFormula(const XmlFormula &f) {
    return callsFunctions(f, timeZoneFunctions);
}
bool xmlTimeFormula(const XmlFormula &f) {
    return callsFunctions(f, timeFunctions);
}
bool xmlProjectConversionFormula(const XmlFormula &f) {
    return callsFunctions(f, projectConversionFunctions);
}
bool hasXmlProjectConversionFunctions(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlProjectConversionFormula(*f.xmlFormula))
                return true;
    return false;
}
bool xmlConversionFormula(const XmlFormula &f) {
    return callsFunctions(f, conversionFunctions);
}
bool hasXmlConversionFunctions(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlConversionFormula(*f.xmlFormula))
                return true;
    return false;
}
bool xmlStringFormula(const XmlFormula &f) {
    return callsFunctions(f, stringFunctions);
}
bool xmlUnicodeFormula(const XmlFormula &f) {
    return callsFunctions(f, unicodeFunctions);
}
bool xmlDateNameFormula(const XmlFormula &f) {
    return callsFunctions(f, dateNameFunctions);
}
bool xmlDateDisplayFormula(const XmlFormula &f) {
    return callsFunctions(f, dateDisplayFunctions);
}
bool xmlNumericDisplayFormula(const XmlFormula &f) {
    return callsFunctions(f, numericDisplayFunctions);
}
bool xmlColorFormula(const XmlFormula &f) {
    return callsFunctions(f, colorFunctions);
}
bool xmlPartitionFormula(const XmlFormula &f) {
    return callsFunctions(f, partitionFunctions);
}
bool xmlDepreciationFormula(const XmlFormula &f) {
    return callsFunctions(f, depreciationFunctions);
}
bool hasXmlDepreciationFunctions(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlDepreciationFormula(*f.xmlFormula))
                return true;
    return false;
}
bool hasXmlPartitionFunctions(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlPartitionFormula(*f.xmlFormula))
                return true;
    return false;
}
bool hasXmlColorFunctions(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlColorFormula(*f.xmlFormula))
                return true;
    return false;
}
bool hasXmlNumericDisplayFunctions(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlNumericDisplayFormula(*f.xmlFormula))
                return true;
    return false;
}
bool hasXmlDateDisplayFunctions(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlDateDisplayFormula(*f.xmlFormula))
                return true;
    return false;
}
bool hasXmlDateNameFunctions(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlDateNameFormula(*f.xmlFormula))
                return true;
    return false;
}
bool hasXmlUnicodeFunctions(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlUnicodeFormula(*f.xmlFormula))
                return true;
    return false;
}
bool hasXmlStringFunctions(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlStringFormula(*f.xmlFormula))
                return true;
    return false;
}
bool xmlMathFormula(const XmlFormula &f) {
    return callsFunctions(f, mathFunctions);
}
bool hasXmlMathFunctions(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlMathFormula(*f.xmlFormula))
                return true;
    return false;
}
bool xmlNumberTextFormula(const XmlFormula &f) {
    return callsFunctions(f, numberTextFunctions);
}
bool hasXmlNumberTextFunctions(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlNumberTextFormula(*f.xmlFormula))
                return true;
    return false;
}
bool hasXmlTimeFunctions(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlTimeFormula(*f.xmlFormula))
                return true;
    return false;
}
namespace {
bool extendedDateFormula(const XmlFormula &f, bool syntaxOnly) {
    if (callsFunctions(f, dateExtractionFunctions))
        return true;
    if (!callsFunctions(f, dateArithmeticFunctions))
        return false;
    try {
        Syntax parser{f.source, 0, 0, {}};
        parser.parse();
        for (const auto &n : parser.nodes) {
            if (n.op != "call" || !dateArithmeticFunctions.contains(n.word))
                continue;
            if (n.word == "datediff" && n.args.size() > 3)
                return true;
            if (syntaxOnly)
                continue;
            if (n.args.empty())
                return true;
            const auto &arg = parser.nodes.at(n.args[0]);
            if (arg.op != "literal" || arg.literal.kind != Kind::Text)
                return true; // Late-bound interval: conservatively use the new writer.
            const auto key = lower(arg.literal.text);
            if (key != "d" && key != "h" && key != "n" && key != "s")
                return true;
        }
    } catch (const std::exception &) {
        return true; // Normal formula validation supplies the syntax diagnostic.
    }
    return false;
}
} // namespace
bool xmlExtendedDateFormula(const XmlFormula &f) {
    return extendedDateFormula(f, false);
}
bool xmlNewDateSyntax(const XmlFormula &f) {
    return extendedDateFormula(f, true);
}
bool hasXmlExtendedDateFunctions(const PlanInput &p, bool activeOnly) {
    for (const auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlExtendedDateFormula(*f.xmlFormula))
                return true;
    return false;
}
bool xmlFormattingFormula(const XmlFormula &f) {
    return callsFunctions(f, formattingFunctions);
}
bool hasXmlFormattingFunctions(const PlanInput &p, bool activeOnly) {
    for (const auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlFormattingFormula(*f.xmlFormula))
                return true;
    return false;
}
bool hasXmlTimeZoneFunctions(const PlanInput &p, bool activeOnly) {
    for (const auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlTimeZoneFormula(*f.xmlFormula))
                return true;
    return false;
}
bool hasXmlAdditionalFunctions(const PlanInput &p, bool activeOnly) {
    for (const auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled) && xmlAdditionalFormula(*f.xmlFormula))
                return true;
    return false;
}
XmlFormulaResult xmlScheduleReference(const PlanInput &p, FieldScope scope, const std::string &referenceKey,
                                      const Task *task, const Resource *resource, const Assignment *assignment) {
    const auto key = xmlBaselineBaseKey(referenceKey);
    calculationCheckpoint();
    if (!xmlComputedReference(key) && !xmlActualReference(key) && !xmlBaselineReference(key))
        return {{}, "지원하지 않는 일정 참조", false};
    auto values = XmlScheduleScope::current(p);
    if (!values)
        return {{}, "계산된 일정을 제공하는 평가 범위가 필요합니다.", false};
    if (scope == FieldScope::Project && xmlDurationReference(referenceKey)) {
        std::vector<std::string> ids;
        if (!xmlBaselineReference(referenceKey))
            for (const auto &t : p.tasks)
                ids.push_back(t.id);
        return xmlSummaryDuration(p, referenceKey, ids, xmlBaselineReference(referenceKey));
    }
    if (const auto slot = xmlBaselineSlot(referenceKey)) {
        auto it = values->numberedBaselines.find(*slot);
        if (it == values->numberedBaselines.end())
            return {{}, "번호별 기준선 평가 문맥이 없습니다.", true};
        values = it->second;
    }
    if (xmlBaselineReference(key)) {
        if (values->externalBaseline) {
            const auto kind = scope == FieldScope::Project      ? "project"
                              : scope == FieldScope::Resource   ? "resource"
                              : scope == FieldScope::Assignment ? "assignment"
                                                                : "task";
            const auto id = scope == FieldScope::Project                    ? ""
                            : scope == FieldScope::Resource && resource     ? resource->id
                            : scope == FieldScope::Assignment && assignment ? assignment->taskId
                            : task                                          ? task->id
                                                                            : "";
            const auto row = values->externalBaselineRows.find(xmlBaselineRowKey(
                kind, id, scope == FieldScope::Assignment && assignment ? assignment->resourceId : ""));
            if (row == values->externalBaselineRows.end())
                return {{}, "채택한 외부 기준선에 객체 값이 없습니다.", true};
            const auto &r = row->second;
            if (key == "builtin:baselinestart" || key == "builtin:baselinefinish") {
                const auto v = key == "builtin:baselinestart" ? r.start : r.finish;
                return {v, v ? "" : "외부 기준선 날짜 미설정", !v};
            }
            const auto v = key == "builtin:baselinecost" ? r.cost : key == "builtin:baselinework" ? r.work : r.duration;
            return {v ? std::optional{key == "builtin:baselinecost" ? formatFieldCost(*v, p.currency)
                                                                    : formatFieldNumber(*v)}
                      : std::nullopt,
                    v ? "" : "외부 기준선의 명시 값이 없습니다.", !v};
        }
        if (!values->baselineIssue.empty())
            return {{}, values->baselineIssue, values->baselineMissing};
        if (key == "builtin:baselinecost") {
            if (!values->baselineCostIssue.empty())
                return {{}, values->baselineCostIssue, false};
            std::optional<std::int64_t> amount;
            bool found = false;
            const auto find = [&](const auto &map, const auto &id) {
                if (auto it = map.find(id); it != map.end()) {
                    found = true;
                    amount = it->second;
                }
            };
            if (scope == FieldScope::Project) {
                found = true;
                amount = values->baselineCost;
            } else if (scope == FieldScope::Task && task)
                find(values->baselineTaskCost, task->id);
            else if (scope == FieldScope::Resource && resource)
                find(values->baselineResourceCost, resource->id);
            else if (scope == FieldScope::Assignment && assignment)
                find(values->baselineAssignmentCost, std::pair{assignment->taskId, assignment->resourceId});
            if (!amount)
                return {{},
                        found ? "동결 원가가 미확정입니다. 단가/고정비 누락 또는 원가 한도를 확인하세요."
                              : "최초 기준선에 연결된 원가가 없습니다.",
                        true};
            return {formatFieldCost(*amount, p.currency), {}, false};
        }
        if (xmlBaselineQuantityReference(key)) {
            std::optional<std::int64_t> amount;
            auto find = [&](const auto &map, const auto &id) {
                if (auto i = map.find(id); i != map.end())
                    amount = i->second;
            };
            if (key == "builtin:baselineduration") {
                if (scope != FieldScope::Task)
                    return {{}, "기준선 기간은 작업 범위만 지원합니다.", false};
                if (task)
                    find(values->baselineDurations, task->id);
            } else if (scope == FieldScope::Project)
                amount = values->baselineWork;
            else if (scope == FieldScope::Task && task)
                find(values->baselineTaskWork, task->id);
            else if (scope == FieldScope::Resource && resource)
                find(values->baselineResourceWork, resource->id);
            else if (scope == FieldScope::Assignment && assignment)
                find(values->baselineAssignmentWork, std::pair{assignment->taskId, assignment->resourceId});
            if (!amount)
                return {{}, "최초 기준선에 연결된 값이 없습니다.", true};
            return {formatFieldNumber(*amount * (key == "builtin:baselinework" ? 10000 : 1000000)), {}, false};
        }
        if (scope == FieldScope::Project) {
            auto v = key == "builtin:baselinestart" ? values->baselineStart : values->baselineFinish;
            return {v, v ? "" : "최초 기준선의 프로젝트 날짜가 없습니다.", !v};
        }
        if (scope == FieldScope::Task && task) {
            auto i = values->baselineTasks.find(task->id);
            if (i == values->baselineTasks.end())
                return {{}, "최초 기준선에 연결된 작업이 없습니다.", true};
            return {key == "builtin:baselinestart" ? i->second.first : i->second.second, {}, false};
        }
        return {{}, "이 범위에서 사용할 수 없는 기준선 날짜 참조", false};
    }
    if (xmlEarlyLateReference(key)) {
        if (!values->issue.empty())
            return {{}, values->issue, values->missing};
        if (!values->earlyLateIssue.empty())
            return {{}, values->earlyLateIssue, false};
        const bool late = key == "builtin:latestart" || key == "builtin:latefinish";
        const bool start = key == "builtin:earlystart" || key == "builtin:latestart";
        if (late && !values->lateIssue.empty())
            return {{}, values->lateIssue, values->lateMissing};
        if (scope == FieldScope::Project) {
            auto v = late ? (start ? values->lateStart : values->lateFinish)
                          : (start ? values->earlyStart : values->earlyFinish);
            return {v, v ? "" : "Early/Late 대상인 미완료 작업이 없습니다.", !v};
        }
        if (scope == FieldScope::Task && task) {
            if (values->earlyLateCompleted.contains(task->id))
                return {{}, "완료 실적에는 미래 Early/Late 날짜를 적용하지 않습니다.", true};
            const auto &rows = late ? values->lateTasks : values->earlyTasks;
            if (auto i = rows.find(task->id); i != rows.end())
                return {start ? i->second.first : i->second.second, {}, false};
            return {{}, "현재 작업의 Early/Late 날짜가 없습니다.", true};
        }
        return {{}, "Early/Late 날짜는 작업/프로젝트 범위만 지원합니다.", false};
    }
    if (xmlRemainingReference(key)) {
        if (!values->remainingIssue.empty())
            return {{}, values->remainingIssue, values->remainingMissing};
        std::optional<std::int64_t> amount;
        auto find = [&](const auto &map, const auto &id) {
            if (auto i = map.find(id); i != map.end())
                amount = i->second;
        };
        if (key == "builtin:remainingduration") {
            if (scope != FieldScope::Task)
                return {{}, "잔여 기간은 작업 범위만 지원합니다.", false};
            if (task)
                find(values->remainingDurations, task->id);
        } else if (scope == FieldScope::Project)
            amount = values->remainingWork;
        else if (scope == FieldScope::Task && task)
            find(values->remainingTaskWork, task->id);
        else if (scope == FieldScope::Resource && resource)
            find(values->remainingResourceWork, resource->id);
        else if (scope == FieldScope::Assignment && assignment)
            find(values->remainingAssignmentWork, std::pair{assignment->taskId, assignment->resourceId});
        if (!amount)
            return {{}, "현재 범위의 잔여 예측 값이 없습니다.", true};
        return {formatFieldNumber(*amount * (key == "builtin:remainingwork" ? 10000 : 1000000)), {}, false};
    }
    if (xmlActualReference(key)) {
        if (!values->actualIssue.empty())
            return {{}, values->actualIssue, values->actualMissing};
        if (xmlActualQuantityReference(key)) {
            std::optional<std::int64_t> amount;
            auto find = [&](const auto &map, const auto &id) {
                if (auto i = map.find(id); i != map.end())
                    amount = i->second;
            };
            if (key == "builtin:actualduration") {
                if (scope != FieldScope::Task)
                    return {{}, "실제 기간은 작업 범위만 지원합니다.", false};
                if (task)
                    find(values->actualDurations, task->id);
            } else {
                if (!values->actualWorkIssue.empty())
                    return {{}, values->actualWorkIssue, false};
                if (scope == FieldScope::Project)
                    amount = values->actualWork;
                else if (scope == FieldScope::Task && task)
                    find(values->actualTaskWork, task->id);
                else if (scope == FieldScope::Resource && resource)
                    find(values->actualResourceWork, resource->id);
                else if (scope == FieldScope::Assignment && assignment)
                    find(values->actualAssignmentWork, std::pair{assignment->taskId, assignment->resourceId});
            }
            if (!amount)
                return {{}, "상태일 기준 보고된 실적 값이 없습니다.", true};
            return {formatFieldNumber(*amount * (key == "builtin:actualwork" ? 10000 : 1000000)), {}, false};
        }
        if (scope == FieldScope::Project) {
            auto v = key == "builtin:actualstart" ? values->actualStart : values->actualFinish;
            return {v,
                    v                              ? ""
                    : key == "builtin:actualstart" ? "상태일 기준 실제 시작 미설정"
                                                   : "전체 작업의 완료 실적이 필요합니다.",
                    !v};
        }
        if (scope == FieldScope::Task && task) {
            auto i = values->actualTasks.find(task->id);
            if (i == values->actualTasks.end())
                return {{}, "상태일 기준 실적 보고가 없습니다.", true};
            auto v = key == "builtin:actualstart" ? i->second.first : i->second.second;
            return {v,
                    v                              ? ""
                    : key == "builtin:actualstart" ? "상태일 기준 실제 시작 미설정"
                                                   : "상태일 기준 실제 완료 미설정",
                    !v};
        }
        return {{}, "이 범위에서 사용할 수 없는 실적 참조", false};
    }
    if (!values->issue.empty())
        return {{}, values->issue, values->missing};
    if (scope == FieldScope::Project) {
        auto v = key == "builtin:start" ? values->start : values->finish;
        return {v, v ? "" : "프로젝트 일정 날짜 미설정", !v};
    }
    if (scope == FieldScope::Task && task) {
        auto i = values->tasks.find(task->id);
        if (i == values->tasks.end())
            return {{}, "현재 작업의 일정 날짜가 없습니다.", true};
        return {key == "builtin:start" ? i->second.first : i->second.second, {}, false};
    }
    return {{}, "이 범위에서 사용할 수 없는 일정 참조", false};
}
std::string xmlFormulaKey(const CustomField &f) {
    if (!f.xmlFormula)
        return "";
    std::string out;
    token(out, f.xmlFormula->source);
    token(out, f.xmlFormula->enabled ? "1" : "0");
    for (const auto &r : f.xmlFormula->references) {
        token(out, r.token);
        token(out, owner(r.scope, r.key));
    }
    return out;
}
std::string xmlFormulaInputKey(const PlanInput &p, const Task *t, const Resource *r, const Assignment *a) {
    std::string out;
    token(out, xmlCalendarInputKey(p, t, r, a));
    token(out, p.currency);
    token(out, p.name);
    token(out, p.startDate);
    token(out, std::to_string(p.tasks.size()));
    token(out, std::to_string(p.resources.size()));
    auto attrs = [&](const auto &values) {
        for (const auto &v : values) {
            token(out, v.key);
            token(out, v.value);
        }
        token(out, "@");
    };
    attrs(p.projectAttributes);
    if (t) {
        token(out, t->id);
        token(out, t->name);
        token(out, t->durationMinutes ? std::to_string(*t->durationMinutes) : "m");
        token(out, t->workUnitMinutes ? std::to_string(*t->workUnitMinutes) : "m");
        attrs(t->attributes);
    }
    if (r) {
        token(out, r->id);
        token(out, r->name);
        token(out, r->maxUnits ? std::to_string(*r->maxUnits) : "unknown-units");
        token(out, r->availableFrom.value_or("unbounded-from"));
        token(out, r->availableTo.value_or("unbounded-to"));
        for (const auto &v : r->availability) {
            token(out, v.from.value_or("unbounded-from"));
            token(out, v.to.value_or("unbounded-to"));
            token(out, v.units ? std::to_string(*v.units) : "unknown");
        }
        for (const auto &day : r->exceptions) {
            token(out, day.date);
            token(out, day.units ? std::to_string(*day.units) : "unknown");
        }
        attrs(r->attributes);
    }
    if (a) {
        token(out, a->taskId);
        token(out, a->resourceId);
        token(out, std::to_string(a->units));
        attrs(a->attributes);
        for (const auto &v : a->rollDownMissing)
            token(out, v);
    }
    return out;
}
XmlFormula bindXmlFormula(const PlanInput &p, FieldScope scope, const std::string &source,
                          const std::vector<XmlFormulaAlias> &extra) {
    XmlFormula result{source, {}, false};
    auto refs = references(source);
    std::set<std::string> seen;
    for (const auto &r : refs) {
        if (!seen.insert(referenceKey(r.name)).second)
            continue;
        auto name = lower(r.name);
        FieldScope target = scope;
        std::string key;
        const auto bound = std::find_if(extra.begin(), extra.end(), [&](const auto &a) {
            return a.bound && referenceKey(a.name) == referenceKey(r.name);
        });
        if (bound != extra.end() && permitted(scope, bound->scope)) {
            result.references.push_back({r.name, bound->key, bound->scope});
            continue;
        }
        if (auto units = bindXmlUnitReference(scope, r.name)) {
            if (permitted(scope, units->scope) && validXmlUnitReference(*units))
                result.references.push_back(*units);
            continue;
        }
        auto actualName = name;
        FieldScope actualScope = scope;
        if (actualName.starts_with("project.")) {
            actualScope = FieldScope::Project;
            actualName = actualName.substr(8);
        } else if (actualName.starts_with("task.") && scope == FieldScope::Assignment) {
            actualScope = FieldScope::Task;
            actualName = actualName.substr(5);
        } else if (actualName.starts_with("resource.") && scope == FieldScope::Assignment) {
            actualScope = FieldScope::Resource;
            actualName = actualName.substr(9);
        }
        const bool actualDuration = actualName == "actual duration" || actualName == "actualduration";
        const bool actualWork = actualName == "actual work" || actualName == "actualwork";
        const bool remainingDuration = actualName == "remaining duration" || actualName == "remainingduration";
        const bool remainingWork = actualName == "remaining work" || actualName == "remainingwork";
        const std::map<std::string, std::string> timing = {
            {"early start", "earlystart"},  {"earlystart", "earlystart"}, {"early finish", "earlyfinish"},
            {"earlyfinish", "earlyfinish"}, {"late start", "latestart"},  {"latestart", "latestart"},
            {"late finish", "latefinish"},  {"latefinish", "latefinish"}};
        if (auto i = timing.find(actualName); i != timing.end()) {
            if (actualScope == FieldScope::Task || actualScope == FieldScope::Project)
                result.references.push_back({r.name, "builtin:" + i->second, actualScope});
            continue;
        }
        const auto baselineKey = xmlBaselineNamedKey(actualName);
        const auto baselineBase = xmlBaselineBaseKey(baselineKey);
        if (!baselineKey.empty()) {
            if (baselineBase == "builtin:baselinework" || baselineBase == "builtin:baselinecost" ||
                actualScope == FieldScope::Task || actualScope == FieldScope::Project)
                result.references.push_back({r.name, baselineKey, actualScope});
            continue;
        }
        // Unsupported numbered slots remain reserved, rather than resolving as an Alias.
        if (actualName.starts_with("baseline") && actualName.size() > 8 && actualName[8] >= '0' && actualName[8] <= '9')
            continue;
        if ((remainingDuration && (actualScope == FieldScope::Task || actualScope == FieldScope::Project)) ||
            remainingWork) {
            result.references.push_back(
                {r.name, remainingDuration ? "builtin:remainingduration" : "builtin:remainingwork", actualScope});
            continue;
        }
        if ((actualDuration && (actualScope == FieldScope::Task || actualScope == FieldScope::Project)) || actualWork) {
            result.references.push_back(
                {r.name, actualDuration ? "builtin:actualduration" : "builtin:actualwork", actualScope});
            continue;
        }
        if ((actualScope == FieldScope::Task || actualScope == FieldScope::Project) &&
            (actualName == "actual start" || actualName == "actualstart" || actualName == "actual finish" ||
             actualName == "actualfinish")) {
            result.references.push_back({r.name,
                                         (actualName == "actual start" || actualName == "actualstart")
                                             ? "builtin:actualstart"
                                             : "builtin:actualfinish",
                                         actualScope});
            continue;
        }
        if ((name == "start" || name == "finish") && (scope == FieldScope::Task || scope == FieldScope::Project)) {
            key = "builtin:" + name;
        } else if (name == "project.start" || name == "project.finish" || name == "project finish") {
            target = FieldScope::Project;
            key = name == "project.start" ? "builtin:start" : "builtin:finish";
        } else if ((name == "task.start" || name == "task.finish") && scope == FieldScope::Assignment) {
            target = FieldScope::Task;
            key = name == "task.start" ? "builtin:start" : "builtin:finish";
        }
        if (!key.empty()) {
            result.references.push_back({r.name, key, target});
            continue;
        }
        const std::map<std::string, std::string> builtins = {{"name", "name"},
                                                             {"summary", "summary"},
                                                             {"duration", "duration"},
                                                             {"work", "work"},
                                                             {"project start", "projectstart"}};
        if (auto i = builtins.find(name);
            i != builtins.end() && ((i->second == "name" && scope != FieldScope::Assignment) ||
                                    (i->second == "summary" && scope == FieldScope::Task) ||
                                    ((i->second == "duration" || i->second == "work") && scope == FieldScope::Task) ||
                                    i->second == "projectstart")) {
            key = "builtin:" + i->second;
        } else {
            if (std::set<std::string>{"task.start",
                                      "task.finish",
                                      "resource.start",
                                      "resource.finish",
                                      "start",
                                      "finish",
                                      "actualstart",
                                      "actualfinish",
                                      "actualduration",
                                      "remainingduration",
                                      "remainingwork",
                                      "remaining duration",
                                      "remaining work",
                                      "task.remainingduration",
                                      "task.remainingwork",
                                      "task.remaining duration",
                                      "task.remaining work",
                                      "resource.remainingduration",
                                      "resource.remainingwork",
                                      "resource.remaining duration",
                                      "resource.remaining work",
                                      "project.remainingduration",
                                      "project.remainingwork",
                                      "project.remaining duration",
                                      "project.remaining work",

                                      "actualwork",
                                      "task.actualduration",
                                      "task.actualwork",
                                      "task.actual duration",
                                      "task.actual work",
                                      "resource.actualduration",
                                      "resource.actualwork",
                                      "resource.actual duration",
                                      "resource.actual work",
                                      "project.actualduration",
                                      "project.actualwork",
                                      "project.actual duration",
                                      "project.actual work",
                                      "task.actualstart",
                                      "task.actualfinish",
                                      "task.actual start",
                                      "task.actual finish",
                                      "resource.actualstart",
                                      "resource.actualfinish",
                                      "resource.actual start",
                                      "resource.actual finish",
                                      "actual start",
                                      "actual finish",
                                      "baseline start",
                                      "baseline finish",
                                      "project finish",
                                      "cost",
                                      "actual cost",
                                      "remaining cost",
                                      "baseline cost",
                                      "remaining duration",
                                      "actual duration",
                                      "remaining work",
                                      "actual work",
                                      "id",
                                      "unique id",
                                      "% complete",
                                      "percent complete",
                                      "calendar",
                                      "project calendar",
                                      "resource names",
                                      "max units",
                                      "units",
                                      "milestone"}
                    .contains(name))
                continue;
            for (auto [prefix, s] : std::vector<std::pair<std::string, FieldScope>>{{"project:", FieldScope::Project},
                                                                                    {"task:", FieldScope::Task},
                                                                                    {"resource:", FieldScope::Resource},
                                                                                    {"field:", scope}})
                if (name.starts_with(prefix)) {
                    target = s;
                    key = "field:" + r.name.substr(prefix.size());
                    break;
                }
            if (key.empty()) {
                auto lookup = r.name;
                for (auto [prefix, s] :
                     std::vector<std::pair<std::string, FieldScope>>{{"project.", FieldScope::Project},
                                                                     {"task.", FieldScope::Task},
                                                                     {"resource.", FieldScope::Resource}})
                    if (lower(lookup).starts_with(prefix)) {
                        target = s;
                        lookup = lookup.substr(prefix.size());
                        break;
                    }
                std::set<std::string> candidates;
                for (const auto &f : customFields(p, target))
                    if (f.xmlIdentity && lower(f.xmlIdentity->fieldName) == lower(lookup))
                        candidates.insert("field:" + f.id);
                for (const auto &a : extra)
                    if (a.scope == target && lower(a.name) == lower(lookup))
                        candidates.insert(a.key);
                if (candidates.empty())
                    for (const auto &f : customFields(p, target))
                        if (lower(f.name) == lower(lookup))
                            candidates.insert("field:" + f.id);
                if (candidates.size() == 1)
                    key = *candidates.begin();
            }
        }
        if (!key.empty() && permitted(scope, target) && (key.starts_with("builtin:") || customField(p, key, target)))
            result.references.push_back({r.name, key, target});
    }
    return result;
}
void validateXmlFormulas(const PlanInput &p) {
    if (!hasXmlFormulas(p))
        return;
    EvaluationScope evaluation;
    std::map<std::string, std::vector<std::string>> edges;
    std::set<std::string> active;
    for (auto s : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, s)) {
            const auto id = owner(s, "field:" + f.id);
            if (f.xmlFormula) {
                const auto &x = *f.xmlFormula;
                const auto sourceReferences = references(x.source);
                std::set<std::string> sourceTokens;
                for (const auto &r : sourceReferences)
                    sourceTokens.insert(referenceKey(r.name));
                if (!f.expression.empty() || x.references.size() > 128)
                    bad("native/XML 식 중복 또는 참조 한도");
                std::set<std::string> names;
                for (const auto &r : x.references) {
                    if (r.token.empty() || r.token.size() > 512 || !sourceTokens.contains(referenceKey(r.token)) ||
                        !names.insert(referenceKey(r.token)).second || !permitted(s, r.scope) ||
                        (!r.key.starts_with("builtin:") && !customField(p, r.key, r.scope)))
                        bad("참조 바인딩의 범위·중복·필드를 확인하세요.");
                    if (r.key.starts_with("builtin:") && r.key != "builtin:name" && r.key != "builtin:summary" &&
                        r.key != "builtin:duration" && r.key != "builtin:work" && r.key != "builtin:projectstart" &&
                        !xmlComputedReference(r.key) && !xmlActualReference(r.key) && !xmlBaselineReference(r.key) &&
                        !xmlUnitReference(r.key))
                        bad("미지원 기본 참조");
                    if (xmlUnitReference(r.key) && !validXmlUnitReference(r))
                        bad("Units/Max Units 참조 범위를 확인하세요.");
                    if (r.key == "builtin:actualduration" && r.scope != FieldScope::Task &&
                        r.scope != FieldScope::Project)
                        bad("실제 기간 참조는 작업/프로젝트 범위만 지원합니다.");
                    if (r.key == "builtin:remainingduration" && r.scope != FieldScope::Task &&
                        r.scope != FieldScope::Project)
                        bad("잔여 기간 참조는 작업/프로젝트 범위만 지원합니다.");
                    if (xmlBaselineBaseKey(r.key) == "builtin:baselineduration" && r.scope != FieldScope::Task &&
                        r.scope != FieldScope::Project)
                        bad("기준선 기간 참조는 작업/프로젝트 범위만 지원합니다.");
                    if ((xmlComputedReference(r.key) || xmlActualReference(r.key) || xmlBaselineReference(r.key)) &&
                        r.key != "builtin:actualwork" && r.key != "builtin:remainingwork" &&
                        xmlBaselineBaseKey(r.key) != "builtin:baselinework" &&
                        xmlBaselineBaseKey(r.key) != "builtin:baselinecost" && r.scope != FieldScope::Task &&
                        r.scope != FieldScope::Project)
                        bad("계산된 일정 참조는 작업/프로젝트 범위만 지원합니다.");
                }
                if (x.enabled) {
                    if (f.type == CustomFieldType::Choice)
                        bad("목록 필드 계산식은 지원하지 않습니다.");
                    auto c = compile(f);
                    (void)c;
                    active.insert(id);
                    for (const auto &r : x.references)
                        if (!r.key.starts_with("builtin:")) {
                            auto target = customField(p, r.key, r.scope);
                            if (target->type == CustomFieldType::Choice)
                                bad("목록 참조는 지원하지 않습니다.");
                            edges[id].push_back(owner(r.scope, r.key));
                        }
                }
            }
            if (!f.expression.empty()) {
                active.insert(id);
                for (const auto &[scope, key] : nativeFormulaReferences(p, s, "field:" + f.id))
                    edges[id].push_back(owner(scope, key));
            }
            if (s != FieldScope::Assignment && f.xmlRollDown && f.xmlRollDown->automatic)
                edges[owner(FieldScope::Assignment, "field:" + f.xmlRollDown->assignmentFieldId)].push_back(id);
        }
    std::map<std::string, int> colors;
    std::map<std::string, int> heights;
    std::function<int(const std::string &, int)> visit = [&](const std::string &id, int depth) {
        calculationCheckpoint();
        if (depth > 64 || colors[id] == 1)
            bad("native/XML/roll-down 참조 순환 또는64단계 초과");
        if (colors[id] == 2)
            return heights[id];
        colors[id] = 1;
        int height = 1;
        for (const auto &e : edges[id])
            height = std::max(height, 1 + visit(e, depth + 1));
        if (height > 64)
            bad("참조 깊이는64단계까지입니다.");
        colors[id] = 2;
        return heights[id] = height;
    };
    for (const auto &id : active)
        visit(id, 0);
}
std::string xmlFormulaProblem(const PlanInput &p, FieldScope s, const std::string &key) {
    try {
        const auto *f = customField(p, key, s);
        if (!f || !f->xmlFormula)
            return "XML 계산식 없음";
        (void)compile(*f);
        return "";
    } catch (const AppError &e) {
        return e.diagnostic().arguments.empty() ? e.code() : e.diagnostic().arguments[0];
    } catch (const Failure &e) {
        return e.message;
    }
}
XmlFormulaResult xmlFormulaValue(const PlanInput &p, FieldScope s, const std::string &key, const Task *t,
                                 const Resource *r, const Assignment *a, const XmlSummaryResolver &summary) {
    const auto *f = customField(p, key, s);
    if (!f || !f->xmlFormula || !f->xmlFormula->enabled)
        return {{}, "비활성 XML 계산식", false};
    EvaluationScope evaluation;
    if (a) {
        if (!t) {
            auto it = std::find_if(p.tasks.begin(), p.tasks.end(), [&](const auto &v) { return v.id == a->taskId; });
            if (it != p.tasks.end())
                t = &*it;
        }
        if (!r) {
            auto it = std::find_if(p.resources.begin(), p.resources.end(),
                                   [&](const auto &v) { return v.id == a->resourceId; });
            if (it != p.resources.end())
                r = &*it;
        }
    }
    std::string cache = "xml-result/v1/";
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &field : customFields(p, scope)) {
            token(cache, owner(scope, field.id));
            token(cache, fieldTypeToken(field.type));
            token(cache, field.expression);
            token(cache, xmlFormulaKey(field));
        }
    token(cache, owner(s, key));
    token(cache, xmlFormulaInputKey(p, t, r, a));
    if (hasXmlScheduleReferences(p, true))
        token(cache, xmlScheduleCacheKey(p));
    if (!summary)
        if (auto c = evaluationCached(cache))
            return *std::static_pointer_cast<const XmlFormulaResult>(c);
    static thread_local std::set<std::string> inProgress;
    auto guard = std::to_string(reinterpret_cast<std::uintptr_t>(&p)) + owner(s, key);
    if (inProgress.size() > 64 || !inProgress.insert(guard).second)
        return {{}, "native/XML 참조 순환·깊이 초과", false};
    struct Pop {
        std::set<std::string> &s;
        std::string key;
        ~Pop() {
            s.erase(key);
        }
    } pop{inProgress, guard};
    XmlFormulaResult result;
    try {
        Context context{p, t, r, a, summary, s, {}, {}, 0};
        V value;
        if (summary) {
            auto c = compile(*f);
            value = context.node(*c, c->root, s);
        } else
            value = context.field(s, key);
        result.value = output(p, *f, value);
        if (!result.value) {
            result.missing = true;
            result.issue = value.text.empty() ? "참조 입력 또는 식 결과 미설정" : value.text;
        }
    } catch (const Failure &e) {
        result.issue = e.message;
    } catch (const AppError &e) {
        result.issue = e.diagnostic().arguments.empty() ? e.code() : e.diagnostic().arguments[0];
    }
    if (!summary)
        evaluationRemember(cache, std::make_shared<const XmlFormulaResult>(result));
    return result;
}
std::string rewriteXmlFormula(const XmlFormula &formula,
                              const std::function<std::string(const XmlFormulaReference &)> &name) {
    std::string out;
    std::size_t at = 0;
    for (const auto &r : references(formula.source)) {
        out += formula.source.substr(at, r.begin - at);
        auto found = std::find_if(formula.references.begin(), formula.references.end(),
                                  [&](const auto &v) { return referenceKey(v.token) == referenceKey(r.name); });
        if (found == formula.references.end())
            out += formula.source.substr(r.begin, r.end - r.begin);
        else {
            auto label = name(*found);
            std::string escaped;
            for (char c : label) {
                if (c == ']' || c == '\\')
                    escaped += '\\';
                escaped += c;
            }
            out += '[' + escaped + ']';
        }
        at = r.end;
    }
    return out + formula.source.substr(at);
}
} // namespace ppm
