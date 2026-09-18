#include "uasm/types.h"

namespace uasm {

bool typeFromName(const std::string& name, Type::Value& out) {
    for (std::size_t i = 0; i < sizeof(kTypeNames) / sizeof(kTypeNames[0]); ++i) {
        if (name == kTypeNames[i]) {
            out = static_cast<Type::Value>(i);
            return true;
        }
    }
    return false;
}

std::size_t sizeOfType(Type::Value t) {
    switch (t) {
        case Type::I8:
        case Type::U8:
            return 1;
        case Type::I16:
        case Type::U16:
            return 2;
        case Type::I32:
        case Type::U32:
        case Type::F32:
            return 4;
        case Type::I64:
        case Type::U64:
        case Type::F64:
        case Type::Ptr:
            return 8;
        case Type::I128:
        case Type::U128:
            return 16;
        case Type::Void:
            return 0;
    }
    return 0;
}

}
