#include "Compiler/IR/context.hpp"

#include <cassert>

int main() {
    using namespace sakuraE::IR;

    IRContext first;
    IRContext second;

    auto* firstInt32 = first.getInt32Ty();
    assert(firstInt32 == first.getInt32Ty());
    assert(first.getPointerTo(firstInt32) == first.getPointerTo(firstInt32));

    auto* secondInt32 = second.getInt32Ty();
    assert(firstInt32 != secondInt32);
    assert(&first.llvmContext() != &second.llvmContext());

    auto* firstTypeInfo = first.typeInfoPool().makeBasicTypeID(TypeID::Int32);
    assert(firstTypeInfo == first.typeInfoPool().makeBasicTypeID(TypeID::Int32));
    assert(firstTypeInfo != second.typeInfoPool().makeBasicTypeID(TypeID::Int32));

    return 0;
}
