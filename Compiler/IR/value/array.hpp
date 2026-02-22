#ifndef SAKURAE_ARRAY_HPP
#define SAKURAE_ARRAY_HPP

#include "Compiler/Error/error.hpp"
#include "value.hpp"
#include <cstddef>
#include <vector>

namespace sakuraE::IR {
    class IRArray {
        IRType* arrType;
        std::vector<IRValue*> arrContent;
    public:
        IRArray(std::vector<IRValue*> arr, PositionInfo info) {
            auto head = arr[0];
            for (std::size_t i = 1; i < arr.size(); i ++) {
                if (!head->getType()->isEqual(arr[i]->getType())) {
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "Cannot create an array with elements of different types.",
                                    info);
                }
            }

            arrType = IRType::getArrayTy(arr[0]->getType(), arr.size());
            arrContent = arr;
        }

        IRType* getType() { return arrType; }
        std::vector<IRValue*>& getArray() { return arrContent; }
        IRValue* getHead() { return arrContent[0]; }
        std::size_t getSize() { return arrContent.size(); }
    };
}

#endif // ! SAKURAE_ARRAY_HPP