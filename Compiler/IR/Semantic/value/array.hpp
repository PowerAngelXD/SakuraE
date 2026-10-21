#ifndef SAKURAE_ARRAY_HPP
#define SAKURAE_ARRAY_HPP

#include "Compiler/Error/error.hpp"
#include "value.hpp"
#include <algorithm>
#include <cstddef>
#include <vector>

namespace sakuraE::IR {
    // 数组字面量对应的编译期聚合（不是 Runtime 分配），作为 Constant 的 IRArray* 负载使用。
    // 元素是非拥有的 IRValue*，Lowering 时直接复用这些原有的 Semantic IR 值。
    // 构造函数虽为 public，但约定只通过 createArray 创建，由全局静态池 arrPool 拥有并负责释放。
    class IRArray {
        std::vector<IRValue*> arrContent;   // 元素列表（非拥有），保证非空
        PositionInfo createInfo;            // 首次创建时的源码位置，用于报错
    public:
        // 校验数组字面量：不允许为空（空数组无法推导元素类型）；
        // 以首元素的语义类型为元素类型，其余元素必须可赋值给它（见 isAssignableTo），否则报 IR 生成错误。
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

        // 数组的语义类型：[首元素类型; 元素个数]，通过元素类型所属的 TypeInfoContext 驻留，
        // 保证与元素处于同一 IRContext。每次调用都会查询驻留表，返回同一个 canonical TypeInfo*。
        TypeInfo* getSemanticType() const {
            auto* element = arrContent.front()->getSemanticType();
            return element->getTypeContext().makeArrayTypeID(element, arrContent.size());
        }
        // 元素访问器；getHead 返回首元素（决定元素类型），getInfo 返回创建位置。
        std::vector<IRValue*>& getArray() { return arrContent; }
        const std::vector<IRValue*>& getArray() const { return arrContent; }
        IRValue* getHead() { return arrContent[0]; }
        std::size_t getSize() { return arrContent.size(); }
        PositionInfo& getInfo() { return createInfo; }

        // 按元素指针逐一比较（身份比较，而非值比较），供 createArray 复用已有数组。
        bool isEqual(std::vector<IRValue*> arr) {
            if (arr.size() != arrContent.size()) return false;

            for (std::size_t i = 0; i < arrContent.size(); i ++) {
                if (arrContent[i] != arr[i]) return false;
            }

            return true;
        }

        // 全局数组池：拥有全部 IRArray；与 IRContext 无关，需显式调用 clearArrayPool 释放。
        static inline std::vector<IRArray*> arrPool;
        // 工厂：若池中已有元素指针序列完全相同的数组则直接复用（此时忽略新的 info），
        // 否则构造（可能因校验失败抛错）并登记入池。线性查找。
        static IRArray* createArray(std::vector<IRValue*> arr, PositionInfo info) {
            for (auto irArr: arrPool) {
                if (irArr->isEqual(arr)) return irArr;
            }

            IRArray* newArr = new IRArray(arr, info);
            arrPool.push_back(newArr);

            return newArr;
        }

        // 释放池中所有数组。调用后任何仍持有 IRArray*（包括 Constant 负载）的引用都会悬空，
        // 因此应与 Constant::clearAll 一同在不再使用 IR 后调用。
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
