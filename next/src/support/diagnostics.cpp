#include "support/diagnostics.hpp"

#include <format>
#include <string_view>

namespace nitwit {

namespace {

std::string_view severityName(Severity severity) {
    switch (severity) {
    case Severity::Note:
        return "note";
    case Severity::Warning:
        return "warning";
    case Severity::Error:
        return "error";
    }
    return "error";
}

} // namespace

void DiagnosticEngine::report(Severity severity, SourceLocation location, std::string message) {
    if (severity == Severity::Error) {
        ++errorCount_;
    }
    diagnostics_.push_back(Diagnostic{severity, location, std::move(message)});
}

std::string DiagnosticEngine::format(const Diagnostic& diagnostic) const {
    if (!diagnostic.location.isValid()) {
        return std::format("{}: {}", severityName(diagnostic.severity), diagnostic.message);
    }
    return std::format("{}: {}: {}", sources_.describe(diagnostic.location), severityName(diagnostic.severity),
                       diagnostic.message);
}

} // namespace nitwit
