#ifndef SAKURAE_TYPE_LOWERING_HPP
#define SAKURAE_TYPE_LOWERING_HPP

#include "session.hpp"
#include "../Semantic/type/type_info.hpp"
#include "../Backend/type/type.hpp"

namespace sakuraE::IR {
    class TypeLowering {
        LoweringSession& session;

    public:
        explicit TypeLowering(LoweringSession& ss): session(ss) {}

        IRType* lower(const TypeInfo* tyInfo);
    };
}

#endif
