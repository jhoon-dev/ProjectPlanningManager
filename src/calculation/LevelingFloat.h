#pragma once
#include "calculation/Scheduler.h"
namespace jobs {
class LevelingFloatScope {
    int previous_;

public:
    explicit LevelingFloatScope(int version);
    ~LevelingFloatScope();
    LevelingFloatScope(const LevelingFloatScope &) = delete;
    LevelingFloatScope &operator=(const LevelingFloatScope &) = delete;
};
bool levelingFloatEnabled();
int levelingFloatVersion();
void calculateSegmentLevelingFloat(const Project &, ScheduleResult &);
void calculateRepartitionLevelingFloat(const Project &, ScheduleResult &, bool logicalOnly = false);
void calculateWorkContourFloat(const Project &, ScheduleResult &);
void calculateLevelingFloat(const Project &, ScheduleResult &);
} // namespace jobs
