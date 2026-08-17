#ifndef CEL_TYPES_HPP
#define CEL_TYPES_HPP

#include <fmt/format.h>
#include <stdexcept>
#include <cstdint>
#include <string>

namespace LIBRARY_NAME {

    using size = std::size_t;

    using i8 = int8_t;
    using i16 = int16_t;
    using i32 = int32_t;
    using i64 = int64_t;
    using in = int;
    
    using u8 = uint8_t;
    using u16 = uint16_t;
    using u32 = uint32_t;
    using u64 = uint64_t;
    using un = unsigned int;

    using f32 = float;
    using f64 = double;

    namespace impl {
        template<typename = void>
        class UnimplementedException : public std::logic_error {
        public:

        private:
        public:
            explicit UnimplementedException(std::string_view p_message) 
            : std::logic_error(fmt::format("UnimplementedException: {}", p_message)) {}
        };
    }

    using UnimplementedException = impl::UnimplementedException<>;
}

#endif // CEL_TYPES_HPP