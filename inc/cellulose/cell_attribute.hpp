#ifndef CEL_CELL_ATTRIBUTE_HPP
#define CEL_CELL_ATTRIBUTE_HPP

#include <tuple>
#include <array>
#include "types.hpp"

namespace LIBRARY_NAME {

    namespace impl {
        /// @brief Cell attributes designed to be access frequently on frame-by-frame basis and thus must be dense to maximize element count per cache line.
        template<typename = void>
        class HotCellAttribute final {
        public:
            u8 m_block_id;
        private:
        public:
        };
        static_assert(
            sizeof(HotCellAttribute<>) <= 4,
            "`HotCellAttribute` must be at most 4 byte to maximize element count per cache line."
        );

        /// @brief  Collection of user-defined cell attribute designed to be access regularly and thus grouped based on type to reduce cache pollution.
        /// @tparam CellCount The number of cell per attribute.
        /// @tparam Attributes The type (or component) that describe a cell's attribute.
        template<size CellCount, typename... Attributes>
        class ColdCellAttributeCollection {
            static_assert(
                ((sizeof(Attributes) <= 8) && ...),
                "Type in `Attributes` must be at most 8 byte."
            );
        public:
            static constexpr size attribute_count = sizeof...(Attributes); 
        private:
            std::tuple<std::array<Attributes, CellCount>...> m_attributes;
        public:
            explicit ColdCellAttributeCollection() = default;

            template<typename Attribute>
            inline auto get() -> std::array<Attribute, CellCount> {
                return std::get<std::array<Attribute, CellCount>>(m_attributes);
            }
        };
    }

    using HotCellAttribute = impl::HotCellAttribute<>;
    template<size CellCount, typename... Attributes>
    using ColdCellAttributeCollection = impl::ColdCellAttributeCollection<CellCount, Attributes...>; 
}

#endif // CEL_CELL_ATTRIBUTE_HPP