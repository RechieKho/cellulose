#ifndef CEL_TYPES_HPP
#define CEL_TYPES_HPP

#include <fmt/format.h>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <string>

namespace cellulose {

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
template <typename = void>
class UnimplementedException : public std::logic_error {
public:
private:
public:
	explicit UnimplementedException(std::string_view p_function_name) : std::logic_error(fmt::format("UnimplementedException: `{}` is not implemented.", p_function_name)) {}
};
} //namespace impl

using UnimplementedException = impl::UnimplementedException<>;

template <typename Function>
struct FunctionArgumentCount;

template <typename ReturnType, typename... Arguments>
struct FunctionArgumentCount<std::function<ReturnType(Arguments...)>> {
	static constexpr size count = sizeof...(Arguments);
};

template <typename Function>
struct FunctionFirstArgumentType;

template <typename ReturnType, typename FirstArgument>
struct FunctionFirstArgumentType<std::function<ReturnType(FirstArgument)>> {
	using Type = FirstArgument;
};

template <typename ReturnType, typename FirstArgument, typename... RestArguments>
struct FunctionFirstArgumentType<std::function<ReturnType(FirstArgument, RestArguments...)>> {
	using Type = FirstArgument;
};
} //namespace cellulose

#define THROW_UNIMPLEMENTED() (throw ::cellulose::UnimplementedException(__func__))

#endif // CEL_TYPES_HPP