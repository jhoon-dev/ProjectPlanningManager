#pragma once
#include "ProgressPolicyFixture.h"
namespace combined_fixture {
inline ppm::Project project(const std::string &profile = "native-fs0-workminutes-v1") {
    auto p = progress_fixture::small(profile);
    p.name = "진척 정책과 평준화";
    ppm::Resource r;
    r.id = "R";
    r.name = "공유 인력";
    r.rates = {{"2026-11-02", 60}, {"2026-11-04", 120}};
    p.resources = {r};
    p.assignments = {{"B", "R", 100}, {"D", "R", 100}};
    for (const auto &task : p.tasks)
        p.taskCosts.push_back({task.id, 0});
    p.progressPolicy = ppm::ProgressPolicySelection{ppm::overrideProgressPolicy, "PM",         "2026-11-03T00:00:00Z",
                                                    "진행 중 수신 관계 검토",    p.documentId, p.revision};
    return p;
}
} // namespace combined_fixture
