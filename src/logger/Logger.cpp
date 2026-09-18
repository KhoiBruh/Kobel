module;

#include <iostream>
#include <format>
#include <string>
#include <string_view>
#include <vector>

export module logger;

export enum class DiagnosticType {
	ERROR,
	WARNING,
	NOTE
};

export struct Diagnostic {
	DiagnosticType type;
	std::string message;
	size_t line = 0;
	size_t col = 0;

	auto format() const {
		std::string_view kind_str = "unknown";
		switch (type) {
			case DiagnosticType::ERROR: kind_str = "error";
				break;
			case DiagnosticType::WARNING: kind_str = "warning";
				break;
			case DiagnosticType::NOTE: kind_str = "note";
				break;
			default: break;
		}
		return std::format("[{}] Line {}, Column {}: {}", kind_str, line, col, message);
	}
};

export struct DiagnosticEngine {
	std::vector<Diagnostic> diagnostics;
	size_t errors_ = 0;

	void report(const DiagnosticType kind, const size_t line, const size_t col, const std::string_view msg) {
		if (kind == DiagnosticType::ERROR) ++errors_;
		diagnostics.push_back({kind, std::string(msg), line, col});
	}

	void error(const size_t line, const size_t col, const std::string_view msg) {
		report(DiagnosticType::ERROR, line, col, msg);
	}

	void warning(const size_t line, const size_t col, const std::string_view msg) {
		report(DiagnosticType::WARNING, line, col, msg);
	}

	bool has_errors() const noexcept {
		return errors_ > 0;
	}

	size_t error_count() const noexcept {
		return errors_;
	}

	void clear() noexcept {
		diagnostics.clear();
		errors_ = 0;
	}

	void print_all(std::ostream &os) const {
		for (const auto &d: diagnostics) {
			os << d.format() << "\n";
		}
	}
};
