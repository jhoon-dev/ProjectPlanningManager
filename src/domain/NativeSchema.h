#pragma once
namespace jobs {
inline constexpr int maximumNativeSchema = 84;
struct Project;
// Read legacy three-argument date arithmetic without treating its previously
// allowed interval expression as new syntax. Writers remain conservative.
int nativeReadSchemaFor(const Project &);
} // namespace jobs
