#include "domain/XmlSchedule.h"
#include "domain/Classification.h"
#include "domain/XmlFormula.h"
#include <string_view>
namespace ppm {
thread_local const XmlScheduleScope::Binding *XmlScheduleScope::current_ = nullptr;
XmlScheduleScope::XmlScheduleScope() : previous_(current_) {
    current_ = &binding_;
}
XmlScheduleScope::XmlScheduleScope(const PlanInput &p, std::shared_ptr<const XmlScheduleValues> values)
    : binding_{&p, std::move(values)}, previous_(current_) {
    current_ = &binding_;
}
XmlScheduleScope::~XmlScheduleScope() {
    current_ = previous_;
}
std::shared_ptr<const XmlScheduleValues> XmlScheduleScope::current(const PlanInput &p) {
    return current_ && current_->input == &p ? current_->values : nullptr;
}
bool xmlActualReference(const std::string &key) {
    return key == "builtin:actualstart" || key == "builtin:actualfinish" || xmlActualQuantityReference(key);
}
bool xmlActualQuantityReference(const std::string &key) {
    return key == "builtin:actualduration" || key == "builtin:actualwork";
}
bool xmlComputedReference(const std::string &key) {
    return key == "builtin:start" || key == "builtin:finish" || xmlRemainingReference(key) ||
           xmlEarlyLateReference(key);
}
bool xmlEarlyLateReference(const std::string &key) {
    return key == "builtin:earlystart" || key == "builtin:earlyfinish" || key == "builtin:latestart" ||
           key == "builtin:latefinish";
}
bool hasXmlEarlyLateReferences(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled))
                for (const auto &r : f.xmlFormula->references)
                    if (xmlEarlyLateReference(r.key))
                        return true;
    return false;
}
bool xmlRemainingReference(const std::string &key) {
    return key == "builtin:remainingduration" || key == "builtin:remainingwork";
}
bool xmlBaselineQuantityReference(const std::string &key) {
    const auto base = xmlBaselineBaseKey(key);
    return base == "builtin:baselineduration" || base == "builtin:baselinework" || base == "builtin:baselinecost";
}
bool xmlBaselineReference(const std::string &key) {
    const auto base = xmlBaselineBaseKey(key);
    return base == "builtin:baselinestart" || base == "builtin:baselinefinish" || base == "builtin:baselineduration" ||
           base == "builtin:baselinework" || base == "builtin:baselinecost";
}
std::optional<int> xmlBaselineSlot(const std::string &key) {
    const std::string_view value(key);
    if (!value.starts_with("builtin:baseline") || value.size() <= 17 || value[16] < '0' || value[16] > '9')
        return {};
    const int slot = value.substr(16, 2) == "10" ? 10 : value[16] - '0';
    const auto metric = value.substr(slot == 10 ? 18 : 17);
    if (metric == "start" || metric == "finish" || metric == "duration" || metric == "work" || metric == "cost")
        return slot;
    return {};
}
std::string xmlBaselineBaseKey(const std::string &key) {
    if (const auto slot = xmlBaselineSlot(key))
        return "builtin:baseline" + key.substr(16 + std::to_string(*slot).size());
    return key;
}
std::string xmlBaselineNamedKey(const std::string &name) {
    std::string key = "builtin:";
    for (auto c : name)
        if (c != ' ')
            key += c >= 'A' && c <= 'Z' ? char(c + 32) : c;
    return xmlBaselineReference(key) ? key : "";
}
std::string xmlBaselineDisplayName(const std::string &key) {
    const auto base = xmlBaselineBaseKey(key);
    if (!xmlBaselineReference(key))
        return {};
    const auto slot = xmlBaselineSlot(key);
    const auto metric = base.substr(16);
    return "Baseline" + (slot ? std::to_string(*slot) : "") + " " +
           (metric == "start"      ? "Start"
            : metric == "finish"   ? "Finish"
            : metric == "duration" ? "Duration"
            : metric == "work"     ? "Work"
                                   : "Cost");
}
bool hasXmlNumberedBaselineReferences(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled))
                for (const auto &r : f.xmlFormula->references)
                    if (xmlBaselineSlot(r.key))
                        return true;
    return false;
}
bool hasXmlBaselineCostReferences(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled))
                for (const auto &r : f.xmlFormula->references)
                    if (xmlBaselineBaseKey(r.key) == "builtin:baselinecost")
                        return true;
    return false;
}
bool hasXmlBaselineReferences(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled))
                for (const auto &r : f.xmlFormula->references)
                    if (xmlBaselineReference(r.key))
                        return true;
    return false;
}
bool hasXmlRemainingReferences(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled))
                for (const auto &r : f.xmlFormula->references)
                    if (xmlRemainingReference(r.key))
                        return true;
    return false;
}
namespace {
bool references(const PlanInput &p, bool activeOnly, bool actual, bool computed) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled))
                for (const auto &r : f.xmlFormula->references)
                    if ((actual && xmlActualReference(r.key)) || (computed && xmlComputedReference(r.key)))
                        return true;
    return false;
}
} // namespace
bool hasXmlActualReferences(const PlanInput &p, bool activeOnly) {
    return references(p, activeOnly, true, false);
}
bool hasXmlActualQuantityReferences(const PlanInput &p, bool activeOnly) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (f.xmlFormula && (!activeOnly || f.xmlFormula->enabled))
                for (const auto &r : f.xmlFormula->references)
                    if (xmlActualQuantityReference(r.key))
                        return true;
    return false;
}
bool hasXmlComputedReferences(const PlanInput &p, bool activeOnly) {
    return references(p, activeOnly, false, true) || hasXmlResourceLoadFunctions(p, true);
}
bool hasXmlScheduleReferences(const PlanInput &p, bool activeOnly) {
    return references(p, activeOnly, true, true) || hasXmlBaselineReferences(p, activeOnly) ||
           hasXmlResourceLoadFunctions(p, true);
}
std::string xmlScheduleCacheKey(const PlanInput &p) {
    auto values = XmlScheduleScope::current(p);
    return values ? values->cacheKey : "xml-schedule:none";
}
std::string xmlScheduleCaption(const PlanInput &p) {
    auto values = XmlScheduleScope::current(p);
    if (!values && hasXmlBaselineReferences(p, true))
        return "기준선 평가 문맥 필요";
    if (!values)
        return hasXmlActualReferences(p, true) ? "일정·실적 평가 문맥 필요" : "일정 계산 필요";
    std::string caption = values->caption;
    if (hasXmlEarlyLateReferences(p, true))
        caption += " · Early/Late: 현재 배치/검증된 후진 날짜 · 예측은 미완료 잔여 구간";
    if (hasXmlRemainingReferences(p, true))
        caption += " / " + values->remainingCaption;
    if (hasXmlActualReferences(p, true))
        caption = hasXmlComputedReferences(p, true) ? caption + " / " + values->actualCaption : values->actualCaption;
    if (hasXmlBaselineReferences(p, true))
        caption += (caption.empty() ? "" : " / ") + values->baselineCaption;
    return caption;
}
} // namespace ppm
