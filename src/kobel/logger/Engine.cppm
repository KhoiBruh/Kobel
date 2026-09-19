module;

#include <string_view>
#include <vector>
#include <ostream>

export module kobel:logger.Engine;

import :logger.Diagnostic;

export namespace kobel::logger {

	struct Engine {
		std::vector<Diagnostic> diagnostics;
		size_t errors = 0;

		[[nodiscard]] bool has_error() const noexcept;

		[[nodiscard]] size_t count() const noexcept;

		void clear() noexcept;

		void print_all(std::ostream &os) const;

		void report(Type type, size_t line, size_t col, std::string_view msg);

		void error(size_t line, size_t col, std::string_view msg);

		void warning(size_t line, size_t col, std::string_view msg);
	};

}
