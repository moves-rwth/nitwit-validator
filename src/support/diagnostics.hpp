#pragma once

// Diagnostics about the input (program or witness) collected during one validation.

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "support/source_manager.hpp"

namespace nitwit {

enum class Severity : std::uint8_t { Note, Warning, Error };

struct Diagnostic {
    Severity severity = Severity::Error;
    SourceLocation location; // may be invalid if the diagnostic has no position
    std::string message;
};

class DiagnosticEngine {
public:
    explicit DiagnosticEngine(const SourceManager& sources) : sources_(sources) {}

    void report(Severity severity, SourceLocation location, std::string message);
    void error(SourceLocation location, std::string message) { report(Severity::Error, location, std::move(message)); }
    void warning(SourceLocation location, std::string message) {
        report(Severity::Warning, location, std::move(message));
    }
    void note(SourceLocation location, std::string message) { report(Severity::Note, location, std::move(message)); }

    [[nodiscard]] bool hasErrors() const { return errorCount_ > 0; }
    [[nodiscard]] std::span<const Diagnostic> diagnostics() const { return diagnostics_; }
    /// Removes the diagnostics reported after the first `count`: those of an attempt whose outcome is discarded.
    void discardAfter(std::size_t count);

    /// "file:line:column: error: message"; the position is left out if the location is invalid.
    [[nodiscard]] std::string format(const Diagnostic& diagnostic) const;

private:
    const SourceManager& sources_;
    std::vector<Diagnostic> diagnostics_;
    std::size_t errorCount_ = 0;
};

} // namespace nitwit
