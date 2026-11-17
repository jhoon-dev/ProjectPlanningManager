#include "domain/CalculationControl.h"
#include "domain/MaterialActuals.h"
#include "domain/ResourcePlan.h"
#include <algorithm>
#include <set>
#include <tuple>
namespace jobs {
namespace {
[[noreturn]] void invalid(const std::string &why) {
    throw AppError(Diagnostic{"MATERIAL_ACTUAL_INVALID", {why}, {}, "MATERIAL_ACTUAL_INVALID"});
}
bool label(const std::string &s, std::size_t max = 4096) {
    return !s.empty() && s.size() <= max && std::any_of(s.begin(), s.end(), [](unsigned char c) { return c > 32; }) &&
           std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
bool sameTransaction(const MaterialActualEntry &a, const MaterialActualEntry &b) {
    return std::tie(a.materialId, a.kind, a.taskId, a.useId, a.receiptId, a.unit, a.openingAt, a.openingMilli) ==
           std::tie(b.materialId, b.kind, b.taskId, b.useId, b.receiptId, b.unit, b.openingAt, b.openingMilli);
}
} // namespace
void validateMaterialActuals(const Project &p) {
    if (p.materialActuals.size() > 100000)
        invalid("원장 100,000행 한도");
    std::map<std::string, const Material *> materials;
    std::map<std::string, const MaterialUse *> uses;
    std::map<std::string, const MaterialReceipt *> receipts;
    std::set<std::string> tasks, ids;
    for (const auto &m : p.resourcePlan.materials)
        materials[m.id] = &m;
    for (const auto &u : p.resourcePlan.uses)
        uses[u.id] = &u;
    for (const auto &r : p.resourcePlan.receipts)
        receipts[r.id] = &r;
    for (const auto &t : p.tasks)
        tasks.insert(t.id);
    std::map<std::string, const MaterialActualEntry *> tails;
    std::uint64_t revision = 0;
    std::set<std::tuple<std::string, std::string, std::string, std::uint64_t>> confirmations;
    for (const auto &e : p.materialActuals) {
        calculationCheckpoint();
        if (!label(e.id, 1024) || !label(e.transactionId, 1024) || !ids.insert(e.id).second ||
            !materials.contains(e.materialId) || !validStamp(e.at) || !validStamp(e.statusAt) || e.at > e.statusAt ||
            !validUtcStamp(e.recordedAt) || !label(e.reporter, 1024) || !label(e.reason) ||
            !label(e.sourceDocumentId, 1024) || !e.recordedRevision || e.recordedRevision > p.revision ||
            e.recordedRevision < revision || e.quantityMilli < 0 || e.quantityMilli > 1000000000000LL)
            invalid("ID·참조·시각·수량·작성 정보");
        revision = e.recordedRevision;
        const auto &m = *materials.at(e.materialId);
        if (!m.stockMilli || e.openingMilli != *m.stockMilli || e.unit != m.unit ||
            e.openingAt != m.stockAt.value_or(p.startDate + " 09:00") || e.at < e.openingAt)
            invalid("실적이 있는 자재의 단위·개시 재고·기준 시각은 변경할 수 없습니다.");
        if ((!e.taskId.empty() && !tasks.contains(e.taskId)) ||
            (!e.useId.empty() && (!uses.contains(e.useId) || uses.at(e.useId)->materialId != e.materialId ||
                                  uses.at(e.useId)->taskId != e.taskId)) ||
            (!e.receiptId.empty() &&
             (!receipts.contains(e.receiptId) || receipts.at(e.receiptId)->materialId != e.materialId)))
            invalid("작업·계획 소모·계획 입고 연결");
        if (e.kind == "receipt") {
            if (!e.taskId.empty() || !e.useId.empty())
                invalid("입고의 작업 연결");
        } else if (e.kind == "consume") {
            if (!e.receiptId.empty())
                invalid("소모의 입고 연결");
        } else if (e.kind == "remaining") {
            if (e.useId.empty() || !e.receiptId.empty() || e.at != e.statusAt)
                invalid("잔여 소모 확인");
        } else if (e.kind == "confirm") {
            if (!e.taskId.empty() || !e.useId.empty() || !e.receiptId.empty() || e.at != e.statusAt)
                invalid("상태일 재고 확인");
        } else
            invalid("원장 종류");
        if ((e.kind == "remaining" || e.kind == "confirm") &&
            !confirmations
                 .emplace(e.kind, e.kind == "remaining" ? e.useId : e.materialId, e.statusAt, e.recordedRevision)
                 .second)
            invalid("동일 제출의 확인 중복");
        const auto old = tails.find(e.transactionId);
        if (old == tails.end()) {
            if (!e.previousEntryId.empty() || e.transactionId != e.id)
                invalid("최초 원장 연결");
        } else if (e.previousEntryId != old->second->id || !sameTransaction(e, *old->second) ||
                   e.statusAt < old->second->statusAt || e.recordedRevision <= old->second->recordedRevision)
            invalid("정정 연결·종류·보고 순서");
        tails[e.transactionId] = &e;
    }
}
std::map<std::string, const MaterialActualEntry *> materialActualsAsOf(const Project &p, const std::string &at) {
    if (!validStamp(at))
        invalid("조회 시각");
    std::map<std::string, const MaterialActualEntry *> out;
    for (const auto &e : p.materialActuals) {
        calculationCheckpoint();
        if (e.statusAt <= at)
            out[e.transactionId] = &e;
    }
    return out;
}
std::vector<ActualMaterialStock> actualMaterialStock(const Project &p, const std::string &at) {
    validateMaterialActuals(p);
    auto current = materialActualsAsOf(p, at);
    std::map<std::string, ActualMaterialStock> stocks;
    std::map<std::string, const MaterialActualEntry *> confirmations;
    std::map<std::string, std::uint64_t> movementRevision;
    for (const auto &m : p.resourcePlan.materials) {
        auto &s = stocks[m.id];
        s.materialId = m.id;
        s.unit = m.unit;
        s.at = at;
        if (at >= m.stockAt.value_or(p.startDate + " 09:00"))
            s.opening = m.stockMilli;
    }
    for (const auto &[id, e] : current) {
        (void)id;
        if (e->at > at)
            continue;
        auto &s = stocks.at(e->materialId);
        if (e->kind == "receipt" || e->kind == "consume")
            movementRevision[e->materialId] = std::max(movementRevision[e->materialId], e->recordedRevision);
        if (e->kind == "receipt")
            s.received += e->quantityMilli;
        if (e->kind == "consume")
            s.consumed += e->quantityMilli;
        if (e->kind == "confirm" && e->statusAt == at) {
            auto &old = confirmations[e->materialId];
            if (!old || e->recordedRevision > old->recordedRevision)
                old = e;
        }
    }
    std::vector<ActualMaterialStock> out;
    for (auto &[id, s] : stocks) {
        if (s.opening)
            s.remaining = *s.opening + s.received - s.consumed;
        if (s.remaining && confirmations.contains(id)) {
            const auto *c = confirmations.at(id);
            s.confirmed = c->quantityMilli == *s.remaining;
            // Any later actual correction invalidates the earlier completeness acknowledgement.
            if (movementRevision[id] > c->recordedRevision)
                s.confirmed = false;
        }
        out.push_back(s);
    }
    return out;
}
void appendMaterialActuals(Project &p, MaterialActualSubmission s, const std::function<std::string()> &newId) {
    if (!p.statusAt || s.entries.empty() || s.entries.size() > 20000 || !validUtcStamp(s.recordedAt) ||
        !label(s.reporter, 1024) || !label(s.reason))
        invalid("상태일·제출 수량·작성자·사유·UTC 시각");
    std::map<std::string, const MaterialActualEntry *> tails;
    for (const auto &e : p.materialActuals)
        tails[e.transactionId] = &e;
    std::set<std::string> corrected, remaining, confirmed;
    std::vector<MaterialActualEntry> added;
    for (auto e : s.entries) {
        calculationCheckpoint();
        const auto m = std::find_if(p.resourcePlan.materials.begin(), p.resourcePlan.materials.end(),
                                    [&](const auto &v) { return v.id == e.materialId; });
        if (m == p.resourcePlan.materials.end() || !m->stockMilli)
            invalid("확인된 개시 재고가 필요합니다.");
        const auto old =
            e.transactionId.empty() ? nullptr : (tails.contains(e.transactionId) ? tails.at(e.transactionId) : nullptr);
        if (!e.transactionId.empty() && (!old || !corrected.insert(e.transactionId).second))
            invalid("정정할 현재 거래를 선택하세요.");
        if (old && e.previousEntryId != old->id)
            invalid("정정 원장이 변경되었습니다. 다시 열어 검토하세요.");
        if (e.kind == "remaining" && !remaining.insert(e.useId).second)
            invalid("한 제출의 잔여 소모 중복");
        if (e.kind == "confirm" && !confirmed.insert(e.materialId).second)
            invalid("한 제출의 재고 확인 중복");
        e.id = newId();
        if (!old) {
            e.transactionId = e.id;
            e.previousEntryId.clear();
        }
        e.statusAt = *p.statusAt;
        e.recordedAt = s.recordedAt;
        e.reporter = s.reporter;
        e.reason = s.reason;
        e.recordedRevision = p.revision + 1;
        e.sourceDocumentId = p.documentId;
        e.unit = m->unit;
        e.openingAt = m->stockAt.value_or(p.startDate + " 09:00");
        e.openingMilli = *m->stockMilli;
        added.push_back(std::move(e));
    }
    auto trial = p;
    ++trial.revision;
    trial.materialActuals.insert(trial.materialActuals.end(), added.begin(), added.end());
    validateMaterialActuals(trial);
    for (const auto &stock : actualMaterialStock(trial, *p.statusAt))
        if (confirmed.contains(stock.materialId) && !stock.confirmed)
            invalid("확인 재고가 실제 원장과 다릅니다.");
    for (const auto &e : added)
        if (e.kind == "remaining") {
            const auto *progress = progressAsOf(p, e.taskId);
            if (progress && progress->actualFinish && e.quantityMilli)
                invalid("완료 작업의 잔여 소모는 0이어야 합니다.");
        }
    p.materialActuals = std::move(trial.materialActuals);
}
Project remainingMaterialInventoryInput(const Project &p) {
    if (!p.statusAt || !p.resourcePlan.autoDelayRemainingForReceipts)
        invalid("상태일·잔여 자재 자동 대기 옵션");
    const auto stocks = actualMaterialStock(p, *p.statusAt);
    const auto current = materialActualsAsOf(p, *p.statusAt);
    std::map<std::string, const MaterialActualEntry *> remaining;
    std::map<std::string, std::int64_t> delivered;
    for (const auto &[id, e] : current) {
        (void)id;
        if (e->kind == "remaining" && e->statusAt == *p.statusAt) {
            auto &r = remaining[e->useId];
            if (!r || e->recordedRevision > r->recordedRevision)
                r = e;
        }
        if (e->kind == "receipt" && !e->receiptId.empty())
            delivered[e->receiptId] += e->quantityMilli;
    }
    auto out = p;
    out.resourcePlan.uses.clear();
    out.resourcePlan.receipts.clear();
    std::set<std::string> used;
    for (const auto &u : p.resourcePlan.uses) {
        if (!remaining.contains(u.id))
            invalid("현재 상태일의 잔여 소모량을 확인하세요: " + u.id);
        const auto *r = remaining.at(u.id);
        const auto *actual = progressAsOf(p, u.taskId);
        if (actual && actual->actualFinish && r->quantityMilli)
            invalid("완료 작업의 잔여 소모는 0이어야 합니다: " + u.taskId);
        used.insert(u.materialId);
        if (r->quantityMilli) {
            auto copy = u;
            copy.quantityMilli = r->quantityMilli;
            out.resourcePlan.uses.push_back(copy);
        }
    }
    for (auto &m : out.resourcePlan.materials) {
        const auto &s =
            *std::find_if(stocks.begin(), stocks.end(), [&](const auto &v) { return v.materialId == m.id; });
        if (used.contains(m.id) && (!s.confirmed || !s.remaining || *s.remaining < 0))
            invalid("상태일 실제 재고를 확인하세요: " + m.id);
        m.stockAt = *p.statusAt;
        m.stockMilli = s.remaining && *s.remaining >= 0 ? s.remaining : std::optional<std::int64_t>{};
    }
    for (const auto &r : p.resourcePlan.receipts)
        if (r.at > *p.statusAt && delivered[r.id] < r.quantityMilli) {
            auto copy = r;
            copy.quantityMilli -= delivered[r.id];
            out.resourcePlan.receipts.push_back(copy);
        }
    out.resourcePlan.autoDelayForReceipts = true;
    return out;
}
} // namespace jobs
