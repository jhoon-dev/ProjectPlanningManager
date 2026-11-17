#include "domain/XmlBaselines.h"
#include "domain/Classification.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <set>
#include <regex>
namespace jobs {
namespace {
[[noreturn]] void invalid(const std::string &text) {
    throw AppError(ErrorCode::FORMULA_INVALID, {"XML 기준선: " + text});
}
} // namespace
std::string xmlBaselineRowKey(const std::string &kind, const std::string &id, const std::string &resource) {
    return kind + ":" + std::to_string(id.size()) + ":" + id + resource;
}
int xmlBaselineTimeType(int slot, const std::string &kind, bool cost) {
    if (slot == 0)
        return (kind == "assignment" ? 4 : kind == "resource" ? 7 : 9) + int(cost);
    return 16 + (slot - 1) * 6 + (kind == "assignment" ? 0 : kind == "resource" ? 4 : 2) + int(cost);
}
bool xmlBaselineTimeCost(int type) {
    return type == 5 || type == 8 || type == 10 || (type >= 17 && type <= 75 && type % 2);
}
std::optional<int> xmlBaselineTimeNumber(int type, const std::string &kind) {
    for (int n = 0; n <= 10; ++n)
        if (type == xmlBaselineTimeType(n, kind, false) || type == xmlBaselineTimeType(n, kind, true))
            return n;
    return {};
}
std::int64_t xmlBaselineMinutes(const std::string &source, std::int64_t maximum) {
    static const std::regex re(R"(^PT(?:(\d+)H)?(?:(\d+)M)?(?:(\d+(?:\.\d{1,6})?)S)?$)");
    std::smatch match;
    if (source.size() > 128 || !std::regex_match(source, match, re) ||
        (!match[1].matched && !match[2].matched && !match[3].matched))
        invalid("기간은 비음수 PT 시/분/초입니다.");
    const auto part = [&](int i) { return match[i].matched ? parseFieldNumber(match[i].str()) : std::int64_t(0); };
    const auto hours = part(1), minutes = part(2), seconds = part(3);
    if (seconds % 60)
        invalid("기간/공수는 소수6자리 분으로 정확히 표현할 수 있어야 합니다.");
    if (hours > maximum / 60 || minutes > maximum - hours * 60 || seconds / 60 > maximum - hours * 60 - minutes)
        invalid("기간/공수 범위를 초과했습니다.");
    return hours * 60 + minutes + seconds / 60;
}
std::int64_t xmlBaselineMoney(const std::string &wire, const std::string &currency) {
    if (wire.empty() || wire.size() > 128)
        invalid("금액 전송 길이 오류");
    const auto scale = currencyDecimals(currency) ? 1 : 100;
    std::size_t at = (wire[0] == '+' || wire[0] == '-') ? 1 : 0, first = at;
    std::int64_t amount = 0;
    while (at < wire.size() && wire[at] >= '0' && wire[at] <= '9') {
        const int digit = wire[at++] - '0';
        if (amount > (maxMoney * scale - digit) / 10)
            invalid("금액 한도 초과");
        amount = amount * 10 + digit;
    }
    if (at == first)
        invalid("XML 금액은 1/100 주요 통화 단위입니다.");
    if (at < wire.size() && wire[at] == '.') {
        ++at;
        auto start = at;
        while (at < wire.size() && wire[at] == '0')
            ++at;
        if (at == start)
            invalid("XML 금액의 최소 통화 정밀도를 확인하세요.");
    }
    if (at != wire.size() || (wire[0] == '-' && amount) || amount % scale)
        invalid("XML 금액의 부호/통화 정밀도를 확인하세요.");
    return amount / scale;
}
std::string xmlBaselineMoneyText(std::int64_t minor, const std::string &currency) {
    if (minor < 0 || minor > maxMoney)
        invalid("금액 한도 초과");
    return std::to_string(minor * (currencyDecimals(currency) ? 1 : 100));
}
void validateXmlBaselines(const PlanInput &p) {
    if (!p.xmlBaselines)
        return;
    const auto &s = *p.xmlBaselines;
    const auto origin = [](const XmlBaselineOrigin &s) {
        if (s.sourceName.empty() || s.sourceName.size() > 1024 || s.sourceSha256.size() != 64 ||
            !std::all_of(s.sourceSha256.begin(), s.sourceSha256.end(),
                         [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ||
            s.reporter.empty() || s.reporter.size() > 512 || s.reason.empty() || s.reason.size() > 2048 ||
            !validUtcStamp(s.recordedAt) || s.weekdays.size() > 7 || s.periods.size() > 16 || s.holidays.size() > 10000)
            invalid("출처·검토 정보·개수 한도를 확인하세요.");
        std::set<int> days;
        for (int day : s.weekdays)
            if (day < 1 || day > 7 || !days.insert(day).second)
                invalid("프로젝트 요일 오류");
        int last = -1;
        for (const auto &[begin, end] : s.periods) {
            if (begin < 0 || end > 1440 || begin >= end || begin < last)
                invalid("프로젝트 근무 구간 오류");
            last = end;
        }
        for (const auto &date : s.holidays)
            if (!validDate(date))
                invalid("프로젝트 휴일 오류");
    };
    origin(s);
    if (s.rows.empty() || s.rows.size() > 100000 || s.origins.size() > 11)
        invalid("기준선 개수 한도");
    std::set<int> numbers;
    for (const auto &r : s.rows)
        numbers.insert(r.number);
    if (!s.origins.empty() && s.origins.size() != numbers.size())
        invalid("번호별 출처가 누락되었습니다.");
    for (const auto &[number, value] : s.origins) {
        if (!numbers.contains(number))
            invalid("번호별 출처 연결 오류");
        origin(value);
    }
    std::set<std::pair<int, std::string>> keys;
    std::size_t periods = 0;
    for (const auto &r : s.rows) {
        calculationCheckpoint();
        if (r.number < 0 || r.number > 10 ||
            (r.kind != "task" && r.kind != "wbs" && r.kind != "project" && r.kind != "resource" &&
             r.kind != "assignment") ||
            r.id.size() > 512 || r.resourceId.size() > 512 || r.xmlUid.empty() || r.xmlUid.size() > 32 ||
            (r.kind != "project" && r.id.empty()) || (r.kind == "project" && !r.id.empty()) ||
            ((r.kind == "assignment") != !r.resourceId.empty()) ||
            !keys.emplace(r.number, xmlBaselineRowKey(r.kind, r.id, r.resourceId)).second)
            invalid("번호·객체 연결·중복 오류");
        for (const auto *date : {&r.start, &r.finish})
            if (*date)
                normalizeFieldDateTime(**date);
        if (r.start && r.finish && *r.start > *r.finish)
            invalid("시작/완료 순서 오류");
        if ((r.duration && (*r.duration < 0 || *r.duration > 5000000LL * fieldNumberScale)) ||
            (r.work && (*r.work < 0 || *r.work > maxWork * 10000)) || (r.cost && (*r.cost < 0 || *r.cost > maxMoney)))
            invalid("기간/공수/원가 범위 오류");
        if ((r.kind == "resource" && (r.start || r.finish || r.duration || !r.flags.empty() ||
                                      (!r.timephased.empty() && !r.timephasedOutside))) ||
            (r.kind == "assignment" && (r.duration || !r.flags.empty())))
            invalid("객체 범위와 다른 값");
        for (const auto &[key, value] : r.flags) {
            if (key == "Interim" || key == "EstimatedDuration") {
                if (value != "0" && value != "1")
                    invalid("Boolean 표시 오류");
            } else if (key == "DurationFormat") {
                if (value.empty() || value.size() > 2 ||
                    !std::all_of(value.begin(), value.end(), [](char c) { return c >= '0' && c <= '9'; }))
                    invalid("기간 표시 오류");
                const std::set<int> formats{3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 19, 20, 21,
                                            35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 51, 52, 53};
                if (!formats.contains(std::stoi(value)))
                    invalid("미지원 기간 표시 번호");
            } else
                invalid("미지원 기준선 표시 필드");
        }
        for (const auto &[key, value] : r.money)
            if ((key != "BCWS" && key != "BCWP" &&
                 (key != "FixedCost" || r.kind == "resource" || r.kind == "assignment")) ||
                value < 0 || value > maxMoney)
                invalid("추가 원가 오류");
        if ((periods += r.timephased.size()) > 100000)
            invalid("시간분포 개수 한도");
        for (const auto &v : r.timephased) {
            calculationCheckpoint();
            if (v.type != xmlBaselineTimeType(r.number, r.kind, false) &&
                v.type != xmlBaselineTimeType(r.number, r.kind, true))
                invalid("번호/객체와 다른 시간분포 종류");
            if (v.uid.empty() || v.uid.size() > 32 ||
                (v.unit && *v.unit != 0 && *v.unit != 1 && *v.unit != 2 && *v.unit != 3 && *v.unit != 5 &&
                 *v.unit != 8) ||
                (v.value && v.value->size() > 128))
                invalid("시간분포 UID/단위/값 오류");
            if (v.start)
                normalizeFieldDateTime(*v.start);
            if (v.finish)
                normalizeFieldDateTime(*v.finish);
            if (v.start && v.finish && *v.start > *v.finish)
                invalid("시간분포 날짜 순서 오류");
            if (v.value && !v.value->empty() && *v.value != "NA" && xmlBaselineTimeCost(v.type) &&
                xmlBaselineMoney(*v.value, p.currency) < 0)
                invalid("시간분포 음수 원가");
            if (v.value && !v.value->empty() && *v.value != "NA" && !xmlBaselineTimeCost(v.type))
                xmlBaselineMinutes(*v.value, maxWork * 10000);
        }
    }
}
const XmlBaselineOrigin &xmlBaselineOrigin(const XmlBaselineSet &s, int number) {
    const auto it = s.origins.find(number);
    return it == s.origins.end() ? static_cast<const XmlBaselineOrigin &>(s) : it->second;
}
std::vector<XmlBaselineLink> xmlBaselineLinks(const XmlBaselineSet &s) {
    std::set<std::pair<std::string, std::string>> keys;
    for (const auto &r : s.rows) {
        calculationCheckpoint();
        keys.emplace(r.kind == "assignment" ? "task" : r.kind, r.id);
        if (r.kind == "assignment")
            keys.emplace("resource", r.resourceId);
    }
    std::vector<XmlBaselineLink> result;
    for (const auto &[kind, id] : keys)
        result.push_back({kind, id, "", false});
    return result;
}
XmlBaselineSet mergeXmlBaselineSets(const std::optional<XmlBaselineSet> &before, const XmlBaselineMergeSubmission &s) {
    PlanInput validation;
    validation.currency = s.currency;
    validation.xmlBaselines = s.incoming;
    validateXmlBaselines(validation);
    if (before) {
        validation.xmlBaselines = before;
        validateXmlBaselines(validation);
    }
    if (!s.confirmed || s.links.size() > 30001)
        invalid("병합 확인과 연결표를 확인하세요.");
    const auto expected = xmlBaselineLinks(s.incoming);
    std::map<std::pair<std::string, std::string>, std::string> links;
    std::set<std::pair<std::string, std::string>> targets;
    for (const auto &l : s.links) {
        if (l.targetId.size() > 512 || (l.exclude && !l.targetId.empty()) ||
            (!l.exclude && l.kind != "project" && l.targetId.empty()) || (l.kind == "project" && !l.targetId.empty()) ||
            !links.emplace(std::pair{l.kind, l.sourceId}, l.exclude ? "" : l.targetId).second ||
            (!l.exclude && !targets.emplace(l.kind, l.targetId).second))
            invalid("모든 객체를 일대일 연결하거나 명시 제외하세요. 대상 중복은 허용하지 않습니다.");
    }
    if (links.size() != expected.size())
        invalid("원본 연결표의 개수가 다릅니다.");
    for (const auto &l : expected)
        if (!links.contains({l.kind, l.sourceId}))
            invalid("원본 연결표가 변경되었습니다.");
    std::set<int> selected;
    for (const auto &r : s.incoming.rows)
        selected.insert(r.number);
    auto result = before.value_or(s.incoming);
    if (!before)
        result.rows.clear();
    else {
        for (const auto &r : before->rows) {
            if (selected.contains(r.number) && !s.replaceExisting)
                invalid("기존 번호 교체를 명시적으로 확인하세요.");
            result.origins[r.number] = xmlBaselineOrigin(*before, r.number);
        }
    }
    std::erase_if(result.rows, [&](const auto &r) { return selected.contains(r.number); });
    static_cast<XmlBaselineOrigin &>(result) = s.incoming;
    for (int n : selected)
        result.origins[n] = xmlBaselineOrigin(s.incoming, n);
    std::set<int> retained;
    for (auto r : s.incoming.rows) {
        calculationCheckpoint();
        if (r.kind == "project") {
            const auto i =
                std::find_if(s.links.begin(), s.links.end(), [](const auto &l) { return l.kind == "project"; });
            if (i->exclude)
                continue;
        } else {
            r.id = links.at({r.kind == "assignment" ? "task" : r.kind, r.id});
            if (r.kind == "assignment")
                r.resourceId = links.at({"resource", r.resourceId});
            if (r.id.empty() || (r.kind == "assignment" && r.resourceId.empty()))
                continue;
        }
        retained.insert(r.number);
        result.rows.push_back(std::move(r));
    }
    if (retained != selected)
        invalid("선택한 각 번호에 하나 이상의 연결된 기준선 객체가 필요합니다.");
    validation.xmlBaselines = result;
    validateXmlBaselines(validation);
    return result;
}
void validateXmlBaselineMergeTarget(const Project &p, const XmlBaselineMergeSubmission &s) {
    if (s.currency != p.currency)
        invalid("XML과 현재 문서의 통화가 다릅니다. 명시 통화 전환 후 다시 검토하세요.");
    std::set<std::pair<std::string, std::string>> objects;
    objects.emplace("project", "");
    for (const auto &t : p.tasks)
        objects.emplace("task", t.id);
    for (const auto &r : p.resources)
        objects.emplace("resource", r.id);
    for (const auto &w : p.wbsNodes)
        objects.emplace("wbs", w.id);
    std::map<std::pair<std::string, std::string>, std::string> links;
    for (const auto &l : s.links) {
        if (!l.exclude && !objects.contains({l.kind, l.targetId}))
            invalid("연결 대상 객체가 현재 문서에 없습니다.");
        links[{l.kind, l.sourceId}] = l.exclude ? "" : l.targetId;
    }
    std::set<std::pair<std::string, std::string>> assignments;
    for (const auto &a : p.assignments)
        assignments.emplace(a.taskId, a.resourceId);
    for (const auto &r : s.incoming.rows) {
        if (!s.replaceExisting && std::any_of(p.baselines.begin(), p.baselines.end(),
                                              [&](const auto &b) { return b.version == std::uint64_t(r.number + 1); }))
            invalid("현재 native 승인 번호보다 외부 값을 우선하려면 기존 번호 교체를 확인하세요.");
        if (r.kind == "assignment") {
            const auto t = links.find({"task", r.id}), u = links.find({"resource", r.resourceId});
            if (t != links.end() && u != links.end() && !t->second.empty() && !u->second.empty() &&
                !assignments.contains({t->second, u->second}))
                invalid("연결된 작업·자원 배정이 현재 문서에 없습니다. 배정을 먼저 만들거나 해당 객체를 제외하세요.");
        }
    }
    mergeXmlBaselineSets(p.xmlBaselines, s);
}
void validateXmlBaselineMerges(const Project &p) {
    if (p.xmlBaselineMerges.size() > 128)
        invalid("기준선 병합 이력 한도는128건입니다.");
    std::set<std::string> ids;
    std::uint64_t revision = 0;
    std::size_t rows = 0;
    for (const auto &e : p.xmlBaselineMerges) {
        calculationCheckpoint();
        if (e.id.empty() || e.id.size() > 1024 || !ids.insert(e.id).second || e.sourceDocumentId.empty() ||
            e.sourceDocumentId.size() > 1024 || e.recordedRevision <= revision || e.recordedRevision > p.revision ||
            (e.currency != "USD" && e.currency != "EUR" && e.currency != "JPY" && e.currency != "KRW"))
            invalid("병합 이력의 출처·revision·통화를 확인하세요.");
        rows += e.incoming.rows.size() + e.after.rows.size() + (e.before ? e.before->rows.size() : 0);
        if (rows > 300000 || mergeXmlBaselineSets(e.before, e) != e.after)
            invalid("기준선 병합 이력의 전후 결과 또는 개수 한도가 다릅니다.");
        revision = e.recordedRevision;
    }
    if (!p.xmlBaselineMerges.empty() && p.xmlBaselineMerges.back().recordedRevision == p.revision &&
        p.xmlBaselines != std::optional{p.xmlBaselineMerges.back().after})
        invalid("현재 기준선과 마지막 병합 결과가 다릅니다.");
}
} // namespace jobs
