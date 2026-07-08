/**
 * @file document_operation.cpp
 * @brief DocumentOperation 实现。
 * @author hxxcxx
 * @date 2026-07-08
 */

#include "document_operation.h"

#include <utility>

namespace mulan::app {

DocumentOperation DocumentOperation::createCurve(std::string name, asset::CurvePrimitive primitive) {
    return DocumentOperation(CreateCurveOperation{ std::move(name), std::move(primitive) });
}

}  // namespace mulan::app
