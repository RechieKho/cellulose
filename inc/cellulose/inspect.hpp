#ifndef CEL_INSPECT_HPP
#define CEL_INSPECT_HPP

#include <concepts>
#include "types.hpp"

namespace cellulose {

    template<typename Function>
    concept IsInspect = requires {
        requires FunctionArgumentCount<Function>::count == 1;
        requires FunctionArgumentCount<
            typename FunctionFirstArgumentType<Function>::ArgumentType
        >::count == 1;
    };

    template <typename GroupInspector>
    auto group_inspect(GroupInspector &&p_inspector) -> void {
        std::forward<GroupInspector>(p_inspector)();
    }

    template <typename GroupInspector, IsInspect FirstInspect, IsInspect... RestInspect>
    auto group_inspect(GroupInspector &&p_inspector, FirstInspect p_first, RestInspect &&...p_rest) -> void {
        p_first([&](auto &&inspected_data) {
            group_inspect(
                    [&p_inspector, data = std::forward<decltype(inspected_data)>(inspected_data)](auto &&...p_accumulated_args) {
                        std::forward<GroupInspector>(p_inspector)(data, std::forward<decltype(p_accumulated_args)>(p_accumulated_args)...);
                    },
                    std::forward<RestInspect>(p_rest)...
                );
        });
    }

}

#endif // CEL_INSPECT_HPP