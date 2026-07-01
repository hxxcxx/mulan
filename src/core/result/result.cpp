#include "result.h"

namespace mulan::core {

Error Error::make(std::string_view msg, std::source_location loc) {
    return Error{0, std::string(msg), loc.file_name(), loc.line()};
}

Error Error::make(int32_t code, std::string_view msg, std::source_location loc) {
    return Error{code, std::string(msg), loc.file_name(), loc.line()};
}

} // namespace mulan::core
