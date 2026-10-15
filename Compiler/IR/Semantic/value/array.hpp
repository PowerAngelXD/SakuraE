#ifndef SAKURAE_ARRAY_HPP
#define SAKURAE_ARRAY_HPP

#include "Compiler/Error/error.hpp"
#include "value.hpp"
#include <algorithm>
#include <cstddef>
#include <vector>

namespace sakuraE::IR {
    class IRArray {
        std::vector<IRValue*> arrContent;
        PositionInfo createInfo;
    public:
        IRArray(std::vector<IRValue*> arr, PositionInfo info) {
            if (arr.empty()) throw SakuraError(OccurredTerm::IR_GENERATING, "Array literal cannot be empty.", info);
            auto head = arr[0];
            for (std::size_t i = 1; i < arr.size(); i ++) {
                if (!isAssignableTo(arr[i]->getSemanticType(), head->getSemanticType())) {
                    throw SakuraError(OccurredTerm::IR_GENERATING,
                                    "Cannot create an array with elements of different types.",
                                    info);
                }
            }

            arrContent = arr;
            createInfo = info;
        }

        TypeInfo* getSemanticType() const {
            auto* element = arrContent.front()->getSemanticType();
            return element->getTypeContext().makeArrayTypeID(element, arrContent.size());
        }
        std::vector<IRValue*>& getArray() { return arrContent; }
        IRValue* getHead() { return arrContent[0]; }
        std::size_t getSize() { return arrContent.size(); }
        PositionInfo& getInfo() { return createInfo; }

        bool isEqual(std::vector<IRValue*> arr) {
            if (arr.size() != arrContent.size()) return false;

            for (std::size_t i = 0; i < arrContent.size(); i ++) {
                if (arrContent[i] != arr[i]) return false;
            }

            return true;
        }

        static inline std::vector<IRArray*> arrPool;
        static IRArray* createArray(std::vector<IRValue*> arr, PositionInfo info) {
            for (auto irArr: arrPool) {
                if (irArr->isEqual(arr)) return irArr;
            }

            IRArray* newArr = new IRArray(arr, info);
            arrPool.push_back(newArr);

            return newArr;
        }

        static void clearArrayPool() {
            for (auto*& arrPtr: arrPool) {
                delete arrPtr;
                arrPtr = nullptr;
            }
            arrPool.clear();
        }
    };
}

#endif /* ! SAKURAE_ARRAY_HPP */
