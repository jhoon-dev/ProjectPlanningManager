#include "domain/XmlSchedule.h"
#include "domain/XmlFormula.h"
#include "domain/FormulaFields.h"
#include <algorithm>
#include <cctype>
#include <functional>
#include <unordered_map>
namespace jobs {
namespace {
using Number = std::int64_t;
constexpr Number limit = 999999999999999LL;
[[noreturn]] void invalid(const std::string &s) {
    throw AppError(ErrorCode::FORMULA_INVALID, {s});
}
struct ArithmeticError {
    std::string message;
};
Number bounded(Number n) {
    if (n < -limit || n > limit)
        throw ArithmeticError{"숫자 범위 초과"};
    return n;
}
// Portable exact 128-bit magnitude multiplication/division, rounded half away from zero.
// Operands are bounded fixed-six decimal integers; no floating point or compiler extension.
Number multiplyDivide(Number a, Number b, Number denominator) {
    if (!denominator)
        throw ArithmeticError{"0으로 나눌 수 없습니다."};
    const bool negative = (a < 0) ^ (b < 0) ^ (denominator < 0);
    const auto magnitude = [](Number n) { return std::uint64_t(n < 0 ? -n : n); };
    std::uint64_t low = 0, high = 0, xLow = magnitude(a), xHigh = 0, second = magnitude(b);
    for (int i = 0; i < 64; ++i) {
        if ((second >> i) & 1) {
            const auto old = low;
            low += xLow;
            high += xHigh + (low < old);
        }
        xHigh = (xHigh << 1) | (xLow >> 63);
        xLow <<= 1;
    }
    const auto divisor = magnitude(denominator);
    std::uint64_t remainder = 0, quotient = 0;
    for (int i = 127; i >= 0; --i) {
        const auto bit = i >= 64 ? (high >> (i - 64)) & 1 : (low >> i) & 1;
        remainder = remainder * 2 + bit;
        const auto digit = remainder >= divisor;
        if (digit)
            remainder -= divisor;
        if (quotient > (std::uint64_t(limit) - digit) / 2)
            throw ArithmeticError{"숫자 범위 초과"};
        quotient = quotient * 2 + digit;
    }
    if (remainder >= divisor - remainder) {
        if (quotient == std::uint64_t(limit))
            throw ArithmeticError{"숫자 범위 초과"};
        ++quotient;
    }
    return negative ? -Number(quotient) : Number(quotient);
}
enum class Op { Literal, Reference, Add, Subtract, Multiply, Divide, Negate, Minimum, Maximum, Coalesce, Absolute };
struct Node {
    Op op = Op::Literal;
    int left = -1, right = -1, reference = -1;
    Number value = 0;
    std::string token, unit;
    int depth = 1;
};
struct Definition {
    FieldScope scope;
    std::string id, unit, expression;
    CustomFieldType type;
    std::vector<Node> nodes;
    int root = -1;
    int state = 0;
    int evaluationDepth = 1;
};
class Parser {
    const std::string &text_;
    std::size_t at_ = 0;
    int depth_ = 0;
    std::vector<Node> &nodes_;
    void spaces() {
        while (at_ < text_.size() &&
               (text_[at_] == ' ' || text_[at_] == '\t' || text_[at_] == '\r' || text_[at_] == '\n'))
            ++at_;
    }
    bool take(char c) {
        spaces();
        if (at_ < text_.size() && text_[at_] == c) {
            ++at_;
            return true;
        }
        return false;
    }
    int node(Node n) {
        calculationCheckpoint();
        if (nodes_.size() >= 128)
            invalid("수식은 최대 128개 연산/값입니다.");
        nodes_.push_back(std::move(n));
        return int(nodes_.size() - 1);
    }
    int binary(Op op, int a, int b) {
        Node n;
        n.op = op;
        n.left = a;
        n.right = b;
        return node(std::move(n));
    }
    int sum() {
        auto a = product();
        for (;;) {
            if (take('+'))
                a = binary(Op::Add, a, product());
            else if (take('-'))
                a = binary(Op::Subtract, a, product());
            else
                return a;
        }
    }
    int product() {
        auto a = atom();
        for (;;) {
            if (take('*'))
                a = binary(Op::Multiply, a, atom());
            else if (take('/'))
                a = binary(Op::Divide, a, atom());
            else
                return a;
        }
    }
    int atom() {
        if (++depth_ > 32)
            invalid("수식 중첩은 32단계까지입니다.");
        spaces();
        int result = -1;
        if (take('+'))
            result = atom();
        else if (take('-')) {
            Node n;
            n.op = Op::Negate;
            n.left = atom();
            result = node(std::move(n));
        } else if (take('(')) {
            result = sum();
            if (!take(')'))
                invalid("닫는 괄호가 필요합니다.");
        } else if (take('[')) {
            std::string reference;
            bool closed = false;
            while (at_ < text_.size()) {
                char c = text_[at_++];
                if (c == ']') {
                    closed = true;
                    break;
                }
                if (c == '\\') {
                    if (at_ >= text_.size() || (text_[at_] != '\\' && text_[at_] != ']'))
                        invalid("참조의 이스케이프를 확인하세요.");
                    c = text_[at_++];
                }
                reference += c;
            }
            if (!closed || reference.empty())
                invalid("참조는 [필드] 형식입니다.");
            Node n;
            n.op = Op::Reference;
            n.token = std::move(reference);
            result = node(std::move(n));
        } else if (at_ < text_.size() && text_[at_] >= '0' && text_[at_] <= '9') {
            const auto first = at_;
            while (at_ < text_.size() && ((text_[at_] >= '0' && text_[at_] <= '9') || text_[at_] == '.'))
                ++at_;
            Node n;
            try {
                n.value = parseFieldNumber(text_.substr(first, at_ - first));
            } catch (const AppError &) {
                invalid("상수는 범위 안의 소수 6자리 숫자입니다.");
            }
            if (take('{')) {
                const auto start = at_;
                while (at_ < text_.size() && text_[at_] != '}')
                    ++at_;
                if (at_ == text_.size() || at_ == start || at_ - start > 128)
                    invalid("단위 상수는 숫자{단위} 형식입니다.");
                n.unit = text_.substr(start, at_++ - start);
                if (n.unit.find_first_of("{}[]\r\n\t") != std::string::npos)
                    invalid("단위 태그를 확인하세요.");
            }
            result = node(std::move(n));
        } else {
            const auto start = at_;
            while (at_ < text_.size() && text_[at_] >= 'a' && text_[at_] <= 'z')
                ++at_;
            const auto name = text_.substr(start, at_ - start);
            Node n;
            if (name == "min")
                n.op = Op::Minimum;
            else if (name == "max")
                n.op = Op::Maximum;
            else if (name == "coalesce")
                n.op = Op::Coalesce;
            else if (name == "abs")
                n.op = Op::Absolute;
            else
                invalid("숫자·참조·괄호·사칙 연산·min/max/coalesce/abs만 지원합니다.");
            if (!take('('))
                invalid("함수 괄호가 필요합니다.");
            n.left = sum();
            if (n.op != Op::Absolute) {
                if (!take(','))
                    invalid("함수 인수는 쉼표로 구분합니다.");
                n.right = sum();
            }
            if (!take(')'))
                invalid("함수 닫는 괄호가 필요합니다.");
            result = node(std::move(n));
        }
        --depth_;
        return result;
    }

public:
    Parser(const std::string &text, std::vector<Node> &nodes) : text_(text), nodes_(nodes) {}
    int parse() {
        if (text_.empty() || text_.size() > 2048)
            invalid("수식은 1~2,048 bytes입니다.");
        if (std::any_of(text_.begin(), text_.end(),
                        [](unsigned char c) { return (c < 32 && c != '\n' && c != '\r' && c != '\t') || c == 127; }))
            invalid("수식의 제어문자를 확인하세요.");
        auto root = sum();
        spaces();
        if (at_ != text_.size())
            invalid("수식 뒤에 알 수 없는 내용이 있습니다.");
        return root;
    }
};
std::string scopeKey(FieldScope scope) {
    return scope == FieldScope::Task         ? "task"
           : scope == FieldScope::Resource   ? "resource"
           : scope == FieldScope::Assignment ? "assignment"
                                             : "project";
}
struct Program {
    std::vector<Definition> definitions;
    std::unordered_map<std::string, int> indexes;
    static std::string key(FieldScope scope, const std::string &id) {
        return scopeKey(scope) + ":" + id;
    }
    int find(FieldScope scope, const std::string &id) const {
        const auto it = indexes.find(key(scope, id));
        if (it == indexes.end())
            invalid("참조 필드를 찾을 수 없습니다: " + id);
        if (definitions[it->second].type != CustomFieldType::Number)
            invalid("숫자 필드만 참조할 수 있습니다: " + id);
        return it->second;
    }
    static std::string builtin(FieldScope scope, const std::string &token) {
        if (scope == FieldScope::Task && (token == "durationHours" || token == "workHours"))
            return "h";
        if (scope == FieldScope::Resource && token == "capacity")
            return "%";
        if (scope == FieldScope::Project && (token == "taskCount" || token == "resourceCount"))
            return {};
        invalid("범위에서 사용할 수 없는 참조입니다: " + token);
    }
    std::string check(int index, int depth = 0) {
        calculationCheckpoint();
        auto &d = definitions.at(std::size_t(index));
        if (d.state == 2)
            return d.unit;
        if (d.state == 1)
            invalid("계산식 참조가 순환합니다: " + d.id);
        if (depth > 64)
            invalid("계산식 참조는 64단계까지입니다.");
        d.state = 1;
        if (!d.expression.empty()) {
            if (d.type != CustomFieldType::Number)
                invalid("계산식은 숫자 필드에만 정의할 수 있습니다.");
            for (auto &n : d.nodes) {
                calculationCheckpoint();
                if (n.op == Op::Literal)
                    continue;
                if (n.op == Op::Reference) {
                    if (n.token.starts_with("field:"))
                        n.reference = find(d.scope, n.token.substr(6));
                    else if (n.token.starts_with("project:"))
                        n.reference = find(FieldScope::Project, n.token.substr(8));
                    else {
                        n.unit = builtin(d.scope, n.token);
                        continue;
                    }
                    n.unit = check(n.reference, depth + 1);
                    n.depth = 1 + definitions[n.reference].evaluationDepth;
                    if (n.depth > 128)
                        invalid("전체 계산 의존 깊이는 128단계까지입니다.");
                    continue;
                }
                const auto &a = d.nodes.at(std::size_t(n.left)).unit;
                n.depth = 1 + d.nodes.at(std::size_t(n.left)).depth;
                if (n.op == Op::Negate || n.op == Op::Absolute) {
                    n.unit = a;
                    continue;
                }
                const auto &b = d.nodes.at(std::size_t(n.right)).unit;
                n.depth = std::max(n.depth, 1 + d.nodes.at(std::size_t(n.right)).depth);
                if (n.depth > 128)
                    invalid("전체 계산 의존 깊이는 128단계까지입니다.");
                if (n.op == Op::Multiply) {
                    if (!a.empty() && !b.empty())
                        invalid("곱셈은 한쪽이 무단위여야 합니다.");
                    n.unit = a.empty() ? b : a;
                } else if (n.op == Op::Divide) {
                    if (b.empty())
                        n.unit = a;
                    else if (a == b)
                        n.unit.clear();
                    else
                        invalid("나눗셈 단위가 일치하지 않습니다.");
                } else {
                    if (a != b)
                        invalid("덧셈·뺄셈·min/max/coalesce의 단위가 일치하지 않습니다.");
                    n.unit = a;
                }
            }
            if (d.nodes.at(std::size_t(d.root)).unit != d.unit)
                invalid("수식 결과 단위와 필드 단위가 일치하지 않습니다: " + d.id);
            d.evaluationDepth = d.nodes.at(std::size_t(d.root)).depth;
            if (d.evaluationDepth > 128)
                invalid("전체 계산 의존 깊이는 128단계까지입니다.");
        }
        d.state = 2;
        return d.unit;
    }
    explicit Program(const PlanInput &p) {
        for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource})
            for (const auto &f : customFields(p, scope)) {
                if (definitions.size() >= 192)
                    invalid("계산식 사전 한도를 초과했습니다.");
                const auto index = int(definitions.size());
                if (!indexes.emplace(key(scope, f.id), index).second)
                    invalid("필드 ID가 중복됩니다.");
                Definition d{scope, f.id, f.unit, f.expression, f.type, {}, -1, 0};
                if (!d.expression.empty())
                    d.root = Parser(d.expression, d.nodes).parse();
                definitions.push_back(std::move(d));
            }
        for (std::size_t i = 0; i < definitions.size(); ++i)
            check(int(i));
    }
};
void field(std::string &key, const std::string &value) {
    key += std::to_string(value.size()) + ":" + value;
}
std::shared_ptr<const Program> program(const PlanInput &p) {
    std::string key = "formula-program/v1/";
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment}) {
        field(key, scopeKey(scope));
        field(key, std::to_string(customFields(p, scope).size()));
        for (const auto &f : customFields(p, scope)) {
            field(key, f.id);
            field(key, fieldTypeToken(f.type));
            field(key, f.unit);
            field(key, f.expression);
            field(key, xmlFormulaKey(f));
        }
    }
    if (auto cached = evaluationCached(key))
        return std::static_pointer_cast<const Program>(cached);
    // A bounded immutable compilation cache also serves individual virtual-table cell requests.
    static thread_local std::string lastKey;
    static thread_local std::shared_ptr<const Program> last;
    if (lastKey != key || !last) {
        auto next = std::make_shared<const Program>(p);
        lastKey = key;
        last = std::move(next);
    }
    evaluationRemember(std::move(key), last);
    return last;
}
struct Context {
    const PlanInput &p;
    const Program &program;
    FieldScope scope;
    const Task *task;
    const Resource *resource;
    std::map<int, FormulaValue> values;
    const std::vector<TaskAttribute> &attributes(FieldScope s) const {
        if (s == FieldScope::Project)
            return p.projectAttributes;
        if (s == FieldScope::Task && task)
            return task->attributes;
        if (s == FieldScope::Resource && resource)
            return resource->attributes;
        invalid("계산식 객체 문맥이 없습니다.");
    }
    FormulaValue evaluate(int index) {
        std::optional<XmlFormulaFrame> frame;
        if (hasXmlFormulas(p))
            frame.emplace();
        calculationCheckpoint();
        if (auto it = values.find(index); it != values.end())
            return it->second;
        const auto &d = program.definitions.at(std::size_t(index));
        FormulaValue value;
        const auto *definition = customField(p, "field:" + d.id, d.scope);
        if (definition && definition->xmlFormula && definition->xmlFormula->enabled) {
            const auto external = xmlFormulaValue(p, d.scope, "field:" + d.id, task, resource);
            value = {external.value ? std::optional{parseFieldNumber(*external.value)} : std::nullopt, external.issue,
                     external.missing};
        } else if (d.expression.empty()) {
            const auto raw = attributeValue(attributes(d.scope), "field:" + d.id);
            value = raw ? FormulaValue{parseFieldNumber(*raw), {}, false} : FormulaValue{{}, "입력 값 미설정", true};
        } else {
            try {
                value = node(d, d.root);
            } catch (const ArithmeticError &e) {
                value = {{}, e.message, false};
            } catch (const AppError &e) {
                value = {{}, e.diagnostic().arguments.empty() ? e.code() : e.diagnostic().arguments[0], false};
            }
        }
        values.emplace(index, value);
        return value;
    }
    FormulaValue builtin(const std::string &token) {
        std::optional<Number> value;
        if (token == "durationHours" && task && task->durationMinutes)
            value = multiplyDivide(*task->durationMinutes, fieldNumberScale, 60);
        else if (token == "workHours" && task && task->workUnitMinutes)
            value = multiplyDivide(*task->workUnitMinutes, fieldNumberScale, 6000);
        else if (token == "capacity" && resource && resource->maxUnits)
            value = Number(*resource->maxUnits) * fieldNumberScale;
        else if (token == "taskCount")
            value = Number(p.tasks.size()) * fieldNumberScale;
        else if (token == "resourceCount")
            value = Number(p.resources.size()) * fieldNumberScale;
        return value ? FormulaValue{value, {}, false} : FormulaValue{{}, "입력 값 미설정", true};
    }
    FormulaValue node(const Definition &d, int index) {
        std::optional<XmlFormulaFrame> frame;
        if (hasXmlFormulas(p))
            frame.emplace();
        calculationCheckpoint();
        const auto &n = d.nodes.at(std::size_t(index));
        if (n.op == Op::Literal)
            return {n.value, {}, false};
        if (n.op == Op::Reference)
            return n.reference >= 0 ? evaluate(n.reference) : builtin(n.token);
        auto a = node(d, n.left);
        if (n.op == Op::Coalesce) {
            if (!a.number && a.missing)
                return node(d, n.right);
            return a;
        }
        if (!a.number)
            return a;
        if (n.op == Op::Negate)
            return {bounded(-*a.number), {}, false};
        if (n.op == Op::Absolute)
            return {*a.number < 0 ? -*a.number : *a.number, {}, false};
        auto b = node(d, n.right);
        if (!b.number)
            return b;
        switch (n.op) {
        case Op::Add:
            return {bounded(*a.number + *b.number), {}, false};
        case Op::Subtract:
            return {bounded(*a.number - *b.number), {}, false};
        case Op::Multiply:
            return {multiplyDivide(*a.number, *b.number, fieldNumberScale), {}, false};
        case Op::Divide:
            return {multiplyDivide(*a.number, fieldNumberScale, *b.number), {}, false};
        case Op::Minimum:
            return {std::min(*a.number, *b.number), {}, false};
        case Op::Maximum:
            return {std::max(*a.number, *b.number), {}, false};
        default:
            invalid("지원하지 않는 계산 연산입니다.");
        }
    }
};
void numericInputKey(std::string &key, const PlanInput &p, FieldScope scope, const std::vector<TaskAttribute> &values) {
    for (const auto &f : customFields(p, scope))
        if (f.type == CustomFieldType::Number && f.expression.empty()) {
            const auto value = attributeValue(values, "field:" + f.id);
            field(key, value ? "v" + *value : "m");
        }
}
} // namespace
bool hasFormulaFields(const PlanInput &p) {
    for (auto scope : {FieldScope::Task, FieldScope::Project, FieldScope::Resource, FieldScope::Assignment})
        for (const auto &f : customFields(p, scope))
            if (calculatedField(f))
                return true;
    return false;
}
void validateFormulaFields(const PlanInput &p) {
    if (!hasFormulaFields(p))
        return;
    EvaluationScope scope;
    calculationCheckpoint();
    (void)program(p);
}
std::shared_ptr<const FormulaValues> formulaValues(const PlanInput &p, FieldScope scope, const Task *task,
                                                   const Resource *resource, const Assignment *assignment) {
    static const auto empty = std::make_shared<const FormulaValues>();
    if (!hasFormulaFields(p))
        return empty;
    EvaluationScope evaluation;
    calculationCheckpoint();
    if (assignment) {
        if (!task) {
            auto i =
                std::find_if(p.tasks.begin(), p.tasks.end(), [&](const auto &t) { return t.id == assignment->taskId; });
            if (i != p.tasks.end())
                task = &*i;
        }
        if (!resource) {
            auto i = std::find_if(p.resources.begin(), p.resources.end(),
                                  [&](const auto &r) { return r.id == assignment->resourceId; });
            if (i != p.resources.end())
                resource = &*i;
        }
    }
    const auto compiled = program(p);
    if ((scope == FieldScope::Task && !task) || (scope == FieldScope::Resource && !resource))
        invalid("계산할 작업·자원이 없습니다.");
    std::string key =
        "formula-values/v1/" + std::to_string(reinterpret_cast<std::uintptr_t>(compiled.get())) + "/" + scopeKey(scope);
    numericInputKey(key, p, FieldScope::Project, p.projectAttributes);
    if (scope == FieldScope::Task)
        numericInputKey(key, p, scope, task->attributes);
    if (scope == FieldScope::Resource)
        numericInputKey(key, p, scope, resource->attributes);
    field(key, std::to_string(p.tasks.size()));
    field(key, std::to_string(p.resources.size()));
    if (task) {
        field(key, task->durationMinutes ? std::to_string(*task->durationMinutes) : "m");
        field(key, task->workUnitMinutes ? std::to_string(*task->workUnitMinutes) : "m");
    }
    if (resource)
        field(key, resource->maxUnits ? std::to_string(*resource->maxUnits) : "m");
    if (hasXmlFormulas(p))
        field(key, xmlFormulaInputKey(p, task, resource, assignment));
    if (hasXmlScheduleReferences(p, true))
        field(key, xmlScheduleCacheKey(p));
    if (auto cached = evaluationCached(key))
        return std::static_pointer_cast<const FormulaValues>(cached);
    Context context{p, *compiled, scope, task, resource, {}};
    auto values = std::make_shared<FormulaValues>();
    for (std::size_t i = 0; i < compiled->definitions.size(); ++i) {
        const auto &d = compiled->definitions[i];
        if (d.scope == scope && !d.expression.empty())
            values->emplace("field:" + d.id, context.evaluate(int(i)));
    }
    for (const auto &f : customFields(p, scope))
        if (f.xmlFormula && f.xmlFormula->enabled) {
            auto v = xmlFormulaValue(p, scope, "field:" + f.id, task, resource, assignment);
            FormulaValue value{{}, v.issue, v.missing, v.value};
            if (v.value && f.type == CustomFieldType::Number)
                value.number = parseFieldNumber(*v.value);
            values->insert_or_assign("field:" + f.id, std::move(value));
        }
    evaluationRemember(std::move(key), values);
    return values;
}
std::optional<std::string> resolvedAttributeValue(const PlanInput &p, const Task &task, const std::string &key) {
    const auto *f = customField(p, key);
    if (!f || !calculatedField(*f))
        return attributeValue(task, key);
    const auto values = formulaValues(p, FieldScope::Task, &task);
    const auto &v = values->at(key);
    return formulaRawValue(v);
}
std::optional<std::string> resolvedAttributeValue(const PlanInput &p, const Resource &resource,
                                                  const std::string &key) {
    const auto *f = customField(p, key, FieldScope::Resource);
    if (!f || !calculatedField(*f))
        return attributeValue(resource.attributes, key);
    const auto values = formulaValues(p, FieldScope::Resource, nullptr, &resource);
    const auto &v = values->at(key);
    return formulaRawValue(v);
}
std::optional<std::string> resolvedProjectAttributeValue(const PlanInput &p, const std::string &key) {
    const auto *f = customField(p, key, FieldScope::Project);
    if (!f || !calculatedField(*f))
        return attributeValue(p.projectAttributes, key);
    const auto values = formulaValues(p, FieldScope::Project);
    const auto &v = values->at(key);
    return formulaRawValue(v);
}
FormulaValue nativeFormulaValue(const PlanInput &p, FieldScope scope, const std::string &key, const Task *task,
                                const Resource *resource) {
    EvaluationScope evaluation;
    const auto compiled = program(p);
    Context context{p, *compiled, scope, task, resource, {}};
    return context.evaluate(compiled->find(scope, key.substr(6)));
}
std::optional<std::string> formulaRawValue(const FormulaValue &v) {
    return v.scalar ? v.scalar : v.number ? std::optional{formatFieldNumber(*v.number)} : std::nullopt;
}
std::string formulaValueText(const FormulaValue &v) {
    const auto raw = formulaRawValue(v);
    return raw ? *raw : v.missing ? "미설정 · " + v.issue : "계산 오류 · " + v.issue;
}
std::vector<std::pair<FieldScope, std::string>> nativeFormulaReferences(const PlanInput &p, FieldScope scope,
                                                                        const std::string &key) {
    std::vector<std::pair<FieldScope, std::string>> out;
    const auto compiled = program(p);
    const auto index = compiled->find(scope, key.substr(6));
    for (const auto &n : compiled->definitions.at(std::size_t(index)).nodes)
        if (n.reference >= 0) {
            const auto &d = compiled->definitions.at(std::size_t(n.reference));
            out.emplace_back(d.scope, "field:" + d.id);
        }
    return out;
}

std::vector<std::pair<std::string, std::string>> formulaReferenceChoices(const PlanInput &p, FieldScope scope) {
    std::vector<std::pair<std::string, std::string>> out;
    auto reference = [](const std::string &prefix, const std::string &id) {
        std::string token = "[" + prefix;
        for (char c : id) {
            if (c == '\\' || c == ']')
                token += '\\';
            token += c;
        }
        return token + "]";
    };
    for (const auto &f : customFields(p, scope))
        if (f.type == CustomFieldType::Number)
            out.emplace_back(f.name + (f.unit.empty() ? "" : " (" + f.unit + ")"), reference("field:", f.id));
    if (scope != FieldScope::Project)
        for (const auto &f : p.projectFields)
            if (f.type == CustomFieldType::Number)
                out.emplace_back("프로젝트 · " + f.name + (f.unit.empty() ? "" : " (" + f.unit + ")"),
                                 reference("project:", f.id));
    if (scope == FieldScope::Task) {
        out.emplace_back("입력 기간 (h)", "[durationHours]");
        out.emplace_back("명시 작업 공수 (h)", "[workHours]");
    }
    if (scope == FieldScope::Resource)
        out.emplace_back("기본 가용률 (%)", "[capacity]");
    if (scope == FieldScope::Project) {
        out.emplace_back("작업 수", "[taskCount]");
        out.emplace_back("자원 수", "[resourceCount]");
    }
    return out;
}
} // namespace jobs
