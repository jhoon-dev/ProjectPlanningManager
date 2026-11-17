#include "domain/LevelingDecision.h"
#include "domain/ResourcePlan.h"
#include "domain/MaterialActuals.h"
#include "domain/Project.h"
#include "domain/CalculationControl.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>

namespace jobs {
namespace {
void invalid() {
    throw AppError(ErrorCode::LEVEL_DECISION_INVALID);
}
bool textValue(const std::string &s, std::size_t limit) {
    return !s.empty() && s.size() <= limit && std::any_of(s.begin(), s.end(), [](unsigned char c) { return c > 32; }) &&
           std::none_of(s.begin(), s.end(), [](unsigned char c) { return c < 32 || c == 127; });
}
std::string utcNow() {
    using namespace std::chrono;
    const auto now = floor<seconds>(system_clock::now());
    const auto day = floor<days>(now);
    const year_month_day date(day);
    const hh_mm_ss time(now - day);
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%04d-%02u-%02uT%02d:%02d:%02dZ", int(date.year()), unsigned(date.month()),
                  unsigned(date.day()), int(time.hours().count()), int(time.minutes().count()),
                  int(time.seconds().count()));
    return buffer;
}
void validateSettings(const std::optional<LevelingSelection> &plan, const std::optional<LevelingSelection> &remaining,
                      const std::optional<std::string> &status, std::uint64_t revision) {
    if (plan && plan->policy == remainingMaterialPolicy)
        invalid();
    if (remaining && remaining->policy == materialArrivalPolicy)
        invalid();
    if ((status && !validStamp(*status)) || (plan && (remaining || status)) || (remaining && !status))
        invalid();
    for (const auto *s : {&plan, &remaining}) {
        if (!*s)
            continue;
        // Historical policies are checked against their own profile, not today's input.
        bool supported = false;
        for (const auto *profile : {"native-fs0-workminutes-v1", pdmProfile, constraintProfile, effortProfile}) {
            PlanInput p;
            p.calculationProfile = profile;
            p.resourcePlan.autoDelayForReceipts = (*s)->policy == materialArrivalPolicy;
            p.resourcePlan.autoDelayRemainingForReceipts = (*s)->policy == remainingMaterialPolicy;
            if (supportsLevelingPolicy(p, (*s)->policy)) {
                validateLevelingSelection(p, **s);
                supported = true;
                break;
            }
        }
        if (!supported || (*s)->sourceRevision > revision)
            invalid();
    }
}
} // namespace
void appendLevelingDecision(Project &p, const Project &before, const std::string &action, const std::string &reason) {
    if (p.planLeveling == before.planLeveling && p.remainingLeveling == before.remainingLeveling)
        return;
    if (p.levelingDecisions != before.levelingDecisions || p.levelingDecisions.size() >= 10000)
        invalid();
    LevelingDecision e;
    e.id = before.documentId + ":leveling:" + std::to_string(p.revision);
    e.previousEntryId = p.levelingDecisions.empty() ? "" : p.levelingDecisions.back().id;
    e.recordedAt = utcNow();
    e.sourceDocumentId = before.documentId;
    e.sourceRevision = before.revision;
    e.recordedRevision = p.revision;
    e.action = action;
    e.reason = reason;
    e.beforeStatusAt = before.statusAt;
    e.afterStatusAt = p.statusAt;
    e.beforePlan = before.planLeveling;
    e.beforeRemaining = before.remainingLeveling;
    e.afterPlan = p.planLeveling;
    e.afterRemaining = p.remainingLeveling;
    p.levelingDecisions.push_back(std::move(e));
}
void validateLevelingDecisions(const Project &p) {
    if (p.levelingDecisions.size() > 10000)
        invalid();
    const LevelingDecision *previous = nullptr;
    std::set<std::string> ids;
    for (const auto &e : p.levelingDecisions) {
        calculationCheckpoint();
        if (!textValue(e.id, 2048) || !ids.insert(e.id).second || !textValue(e.sourceDocumentId, 1024) ||
            !validUtcStamp(e.recordedAt) || !textValue(e.reason, 4096) || !e.sourceRevision ||
            e.sourceRevision >= 9007199254740991ULL || e.recordedRevision != e.sourceRevision + 1 ||
            e.recordedRevision > p.revision || e.previousEntryId != (previous ? previous->id : "") ||
            (previous && (e.sourceRevision < previous->recordedRevision || e.beforePlan != previous->afterPlan ||
                          e.beforeRemaining != previous->afterRemaining)) ||
            (e.beforePlan == e.afterPlan && e.beforeRemaining == e.afterRemaining))
            invalid();
        validateSettings(e.beforePlan, e.beforeRemaining, e.beforeStatusAt, e.sourceRevision);
        validateSettings(e.afterPlan, e.afterRemaining, e.afterStatusAt, e.recordedRevision);
        const bool before = e.beforePlan || e.beforeRemaining, after = e.afterPlan || e.afterRemaining;
        if (e.action == "adopt") {
            const auto &selected = e.afterPlan ? e.afterPlan : e.afterRemaining;
            if (!selected || selected->sourceDocumentId != e.sourceDocumentId ||
                selected->sourceRevision != e.sourceRevision || selected->reason != e.reason)
                invalid();
        } else if (e.action == "release" || e.action == "invalidate") {
            if (!before || after)
                invalid();
        } else if (e.action != "undo" && e.action != "redo")
            invalid();
        previous = &e;
    }
    if (previous && (p.planLeveling != previous->afterPlan || p.remainingLeveling != previous->afterRemaining))
        invalid();
}
} // namespace jobs
