module;

#include <cstdint>
#include <cstring>
#include <vector>
#include <utility>
#include <string_view>
#include <span>
#include <type_traits>
#include <initializer_list>

export module ast.arena;

export struct Arena {
	struct Block {
		uint8_t *data;
		size_t size;
		size_t used;

		explicit Block(size_t sz) : size(sz), used(0) {
			data = new uint8_t[sz];
		}

		~Block() {
			delete[] data;
		}

		Block(const Block &) = delete;

		Block &operator=(const Block &) = delete;

		Block(Block &&other) noexcept : data(other.data), size(other.size), used(other.used) {
			other.data = nullptr;
			other.size = 0;
			other.used = 0;
		}

		Block &operator=(Block &&other) noexcept {
			if (this != &other) {
				delete[] data;
				data = other.data;
				size = other.size;
				used = other.used;
				other.data = nullptr;
				other.size = 0;
				other.used = 0;
			}
			return *this;
		}
	};

	std::vector<Block> blocks;
	size_t block_size;

	explicit Arena(size_t bs = 65536) : block_size(bs) {
		blocks.emplace_back(bs);
	}

	void *allocate(size_t size, size_t align = 8) {
		if (blocks.empty()) blocks.emplace_back(block_size);

		Block *current = &blocks.back();
		size_t aligned_used = (current->used + align - 1) & ~(align - 1);

		if (aligned_used + size > current->size) {
			size_t new_block_size = block_size > size ? block_size : size;
			blocks.emplace_back(new_block_size);
			current = &blocks.back();
			aligned_used = 0;
		}

		current->used = aligned_used + size;
		return current->data + aligned_used;
	}

	template<typename T, typename... Args>
	T *alloc(Args &&... args) {
		void *ptr = allocate(sizeof(T), alignof(T));
		return new(ptr) T(std::forward<Args>(args)...);
	}

	template<typename T>
	std::span<T> alloc_span(std::span<const T> src) {
		if (src.empty()) return {};
		T *ptr = static_cast<T *>(allocate(sizeof(T) * src.size(), alignof(T)));
		if constexpr (std::is_trivially_copyable_v<T>) {
			std::memcpy(ptr, src.data(), sizeof(T) * src.size());
		} else {
			for (size_t i = 0; i < src.size(); ++i) {
				new(ptr + i) T(src[i]);
			}
		}
		return std::span<T>(ptr, src.size());
	}

	template<typename T>
	std::span<T> alloc_span(std::span<T> src) {
		return alloc_span(std::span<const T>(src));
	}

	template<typename T>
	std::span<T> alloc_span(std::initializer_list<T> list) {
		return alloc_span(std::span<const T>(list.begin(), list.size()));
	}

	template<typename T>
	std::span<T> alloc_span(std::vector<T> &vec) {
		if (vec.empty()) return {};
		T *ptr = static_cast<T *>(allocate(sizeof(T) * vec.size(), alignof(T)));
		if constexpr (std::is_trivially_copyable_v<T>) {
			std::memcpy(ptr, vec.data(), sizeof(T) * vec.size());
		} else {
			for (size_t i = 0; i < vec.size(); ++i) {
				new(ptr + i) T(std::move(vec[i]));
			}
		}
		return std::span<T>(ptr, vec.size());
	}

	template<typename T>
	std::span<T> alloc_span(std::vector<T> &&vec) {
		return alloc_span(vec);
	}

	template<typename T>
	std::span<T> alloc_span(const std::vector<T> &vec) {
		return alloc_span(std::span<const T>(vec.data(), vec.size()));
	}

	std::string_view alloc_string(const std::string_view str) {
		if (str.empty()) return {};
		const auto ptr = static_cast<char *>(allocate(str.size(), 1));
		std::memcpy(ptr, str.data(), str.size());
		return std::string_view{ptr, str.size()};
	}
};
