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

	void report(const DiagnosticType kind, const size_t line, const size_t col, const std::string_view msg) {
		diagnostics.push_back({kind, std::string(msg), line, col});
	}

	void error(const size_t line, const size_t col, const std::string_view msg) {
		report(DiagnosticType::ERROR, line, col, msg);
	}

	void warning(const size_t line, const size_t col, const std::string_view msg) {
		report(DiagnosticType::WARNING, line, col, msg);
	}

	bool has_errors() const {
		for (const auto &d: diagnostics) {
			if (d.type == DiagnosticType::ERROR) return true;
		}
		return false;
	}

	auto error_count() const {
		size_t count = 0;
		for (const auto &d: diagnostics) {
			if (d.type == DiagnosticType::ERROR) count++;
		}
		return count;
	}

	void clear() {
		diagnostics.clear();
	}

	void print_all(std::ostream &os) const {
		for (const auto &d: diagnostics) {
			os << d.format() << "\n";
		}
	}
};
