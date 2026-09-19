module;

#include <format>
#include <string>
#include <string_view>

module kobel;

namespace kobel::logger {

	std::string Diagnostic::format() const {
		std::string_view type_str = "unknown";
		switch (type) {
			case Type::ERROR:
				type_str = "error";
				break;

			case Type::WARNING:
				type_str = "warning";
				break;

			case Type::NOTE:
				type_str = "note";
				break;

			default:
				break;
		}

		return std::format("[{}] Line {}, Column {}: {}", type_str, line, col, message);
	}

}
