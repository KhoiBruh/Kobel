module;

#include <string>
#include <ostream>

module kobel;

namespace kobel::logger {

	bool Engine::has_error() const noexcept {
		return errors > 0;
	}

	size_t Engine::count() const noexcept {
		return errors;
	}

	void Engine::clear() noexcept {
		diagnostics.clear();
		errors = 0;
	}

	void Engine::print_all(std::ostream &os) const {
		for (const auto &diagnostic: diagnostics)
			os << diagnostic.format() << '\n';
	}

	void Engine::report(
		const DiagnosticType type,
		const size_t line,
		const size_t col,
		const std::string_view msg
	) {
		if (type == DiagnosticType::ERROR) errors++;
		const Diagnostic diagnostic {
			.type = type,
			.message = std::string(msg),
			.line = line,
			.col = col
		};
		diagnostics.push_back(diagnostic);
	}

	void Engine::error(
		const size_t line,
		const size_t col,
		const std::string_view msg
	) {
		report(DiagnosticType::ERROR, line, col, msg);
	}

	void Engine::warning(
		const size_t line,
		const size_t col,
		const std::string_view msg
	) {
		report(DiagnosticType::WARNING, line, col, msg);
	}

}
