module;

#include <string>
#include <format>

export module kobel:logger.Diagnostic;

import :logger.Type;

export namespace kobel::logger {

	struct Diagnostic {
		Type type;
		std::string message;
		size_t line = 0;
		size_t col = 0;

		[[nodiscard]] std::string format() const;
	};

}
