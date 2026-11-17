#pragma once
#include "domain/Project.h"
#include <map>
namespace jobs {
struct RollUpCell {
    std::optional<std::string> value;
    std::string issue, origin;
    std::size_t missing = 0, leaves = 0;
};
struct RollUpGroup {
    std::string id, parentId;
    std::vector<std::size_t> rows;
};
bool hasXmlRollUp(const PlanInput &);
void validateXmlRollUp(const PlanInput &);
bool rollUpSupported(CustomFieldType, int);
std::string rollUpMethodName(int);
std::string rollUpPath(const std::vector<std::string> &);
class RollUpValues {
public:
    explicit RollUpValues(const PlanInput &, FieldScope = FieldScope::Task);
    RollUpValues(const PlanInput &, FieldScope, std::vector<RollUpGroup>);
    const std::vector<const CustomField *> &fields() const {
        return fields_;
    }
    const RollUpCell &cell(const std::string &groupId, const std::string &key) const;
    std::string describe(const std::string &groupId) const;

private:
    const PlanInput &p_;
    FieldScope scope_;
    std::vector<const CustomField *> fields_;
    std::map<std::string, std::map<std::string, RollUpCell>> cells_;
    void build(std::vector<RollUpGroup>, bool snapshots);
};
} // namespace jobs
