#ifndef CEL_BLOCK_HPP
#define CEL_BLOCK_HPP

#include <vector>
#include <ankerl/unordered_dense.h>

#include "types.hpp"
#include "inspect.hpp"

namespace cellulose {

    using BlockID = u16;

    namespace impl {

        /// @brief Read-only data for block.
        template<typename = void>
        class Block final {
        public:
            std::string name; //!< Name of the block.
        };

        /// @brief Registry of blocks.
        /// Instead of directly mapping the name to the block,
        /// We split it into `NameIDMap` and `Store`
        /// to allow access from both block id and block name
        /// with priority to access via block id.
        /// This is to allow renderer access the block via block id
        /// from `HotCellAttribute`, while allow other to access with a more
        /// user-friendly way, which is name of the block.
        template<typename = void>
        class BlockRegistry final {
        public:
            /// @brief Map from name of block to index in corresponding `BlockRegistry`.
            using NameIDMap = ankerl::unordered_dense::map<std::string, BlockID>;
            /// @brief Stores Blocks and map Block ID to block.
            using Store = std::vector<Block<>>;
        private:
            NameIDMap m_name_id_map;
            Store m_store;
        public:
        };

    }

}

#endif // CEL_BLOCK_HPP