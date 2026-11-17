#pragma once
#include <cstdint>
#include <string>
namespace jobs {
// Arguments are whole numbers, already rounded to even by the formula evaluator.
std::string xmlPartition(std::int64_t number, std::int64_t start, std::int64_t stop, std::int64_t interval);
} // namespace jobs
