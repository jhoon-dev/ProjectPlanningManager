#include "application/Document.h"
#include "formatting/DiagnosticText.h"
#include "persistence/JsonProjectStore.h"
#include "persistence/ProjectFile.h"
#include <QCoreApplication>
#include <iostream>

namespace {
void check(bool value, const char *reason) {
    if (!value)
        throw std::logic_error(reason);
}
} // namespace
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    try {
        ppm::AppError error(ppm::ErrorCode::TASK_DURATION_INVALID, {},
                            ppm::ErrorContext{.taskIds = {"T1"}, .resourceIds = {"R1"}, .calendarIds = {"C1"}});
        check(error.code() == "TASK_DURATION_INVALID" && std::string(error.what()) == error.code(),
              "exception is not a stable code");
        const auto korean = ppm::formatDiagnostic(error.diagnostic());
        const auto english = ppm::formatDiagnostic(error.diagnostic(), ppm::DiagnosticLanguage::English);
        check(korean != english && korean.find("T1") != std::string::npos && english.find("R1") != std::string::npos &&
                  english.find("C1") != std::string::npos,
              "language/context rendering failed");
        auto nested = ppm::diagnosticFromException(error, "TASK_EDIT_FAILED", {"T2"});
        check(nested.code == "TASK_EDIT_FAILED" && nested.causes.size() == 1 &&
                  nested.causes[0].code == "TASK_DURATION_INVALID" && nested.taskIds.size() == 2,
              "cause/context discarded");
        auto unknown = ppm::diagnosticFromException(std::runtime_error("extension detail"));
        check(unknown.code == "INTERNAL_ERROR" && unknown.arguments.at(0) == "extension detail",
              "unknown exception evidence hidden");
        ppm::Diagnostic injection{"unknown", {"{0} <b> & path"}, {}};
        check(ppm::diagnosticMessage(injection).find("{0} <b> & path") != std::string::npos,
              "arguments were recursively interpolated");
        check(ppm::diagnosticRecommendation({"POLICY_FS", {}, {}}).empty(), "policy info suggests correction");
        check(ppm::diagnosticRecommendation({"WORKSPACE_OVERLOAD", {}, {}}).find("공유 용량") != std::string::npos,
              "workspace overload incorrectly suggests changing task work/duration");
        check(ppm::diagnosticRecommendation({"WORKSPACE_CANCELLED", {}, {}}).empty(),
              "cancellation suggests correcting valid inputs");
        ppm::JsonProjectStore files;
        ppm::Document document(files);
        try {
            document.renameTask(0, "");
            check(false, "closed document edited");
        } catch (const ppm::AppError &caught) {
            check(caught.code() == "DOCUMENT_NOT_EDITABLE", "guard code changed");
        }
        document.create("Diagnostic fixture", "2026-10-26", {"Task"});
        try {
            document.setDuration(0, -1);
            check(false, "invalid duration accepted");
        } catch (const ppm::AppError &caught) {
            check(caught.code() == "TASK_DURATION_INVALID", "duration code changed");
        }
        try {
            (void)ppm::decodeProjectFile("{invalid");
            check(false, "invalid JSON accepted");
        } catch (const ppm::AppError &caught) {
            check(caught.code() == "FILE_JSON_INVALID", "JSON parser code changed");
        }
        const auto before = document.snapshot();
        auto prepared = ppm::Document::prepareEdit(document.makeEditRequest(ppm::DurationEdit{before->tasks[0].id, -1}),
                                                   std::make_shared<ppm::CalculationControl>());
        check(prepared->failure() && !prepared->failure()->code.empty() && document.snapshot() == before,
              "worker failure lost structured context or changed source");
        std::cout
            << "PASS: stable codes, localization, related IDs, nested causes, parser and prepared edit failures\n";
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << ppm::errorText(error) << '\n';
        return 1;
    }
}
