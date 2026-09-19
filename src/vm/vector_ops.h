#pragma once

#include <cstddef>

#include "uasm/ast.h"
#include "uasm/value.h"

namespace uasm {

std::size_t vectorLaneCount(Type::Value width, Type::Value lane);

Value vectorGetLane(const Value& v, Type::Value lane, std::size_t index);

void vectorSetLane(Value& v, Type::Value lane, std::size_t index, const Value& laneValue);

bool vectorOpIsMemory(Opcode::Value op);

Value vectorCompute(Opcode::Value op, Type::Value width, Type::Value lane, const Value* args, std::size_t argCount);

Value vectorComputeWide(Opcode::Value op, Type::Value width, Type::Value lane, const Value* args,
                        std::size_t argCount);

Value vectorComputeMasked(Opcode::Value op, Type::Value width, Type::Value lane, const Value* args,
                          std::size_t argCount, bool zeroing);

bool vectorOpIsWide(Opcode::Value op);

int vectorMaskedBase(Opcode::Value op, Opcode::Value& baseOp);

}
