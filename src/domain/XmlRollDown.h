#pragma once
#include "domain/Project.h"
#include <map>
#include <unordered_map>
namespace ppm {
bool hasXmlRollDown(const PlanInput &);
void validateXmlRollDown(const PlanInput &);
void extendRollDownLookups(PlanInput &);
bool rollDownCompatible(const CustomField &, const CustomField &);
class RollDownValues {
public:
    explicit RollDownValues(const PlanInput &);
    std::optional<std::string> value(const Assignment &, const std::string &key) const;
    std::vector<TaskAttribute> attributes(const Assignment &) const;
    std::string origin(const Assignment &, const std::string &key) const;
    const CustomField *source(const std::string &key) const;

private:
    struct Binding {
        FieldScope scope;
        const CustomField *source, *target;
        std::unordered_map<std::string, std::string> choices;
    };
    const PlanInput &p_;
    std::map<std::string, Binding> bindings_;
    std::unordered_map<std::string, const Task *> tasks_;
    std::unordered_map<std::string, const Resource *> resources_;
};
} // namespace ppm
