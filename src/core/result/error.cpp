#include "error.h"

namespace mulan {

Error Error::make(std::string_view msg, std::source_location loc) {
    return Error{ static_cast<int32_t>(ErrorCode::Generic), std::string(msg), loc.file_name(), loc.line() };
}

Error Error::make(ErrorCode code, std::string_view msg, std::source_location loc) {
    return Error{ static_cast<int32_t>(code), std::string(msg), loc.file_name(), loc.line() };
}

Error Error::make(int32_t code, std::string_view msg, std::source_location loc) {
    return Error{ code, std::string(msg), loc.file_name(), loc.line() };
}

}  // namespace mulan
