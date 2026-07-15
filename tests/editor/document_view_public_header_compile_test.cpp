/**
 * @file document_view_public_header_compile_test.cpp
 * @brief 验证 editor 公开头可在不接触私有包含目录时独立编译。
 * @author hxxcxx
 * @date 2026-07-15
 */

#include <mulan/editor/command/builtin_commands.h>
#include <mulan/editor/command/command.h>
#include <mulan/editor/command/command_manager.h>
#include <mulan/editor/document/document_session.h>
#include <mulan/editor/document/document_view.h>

#include <type_traits>

static_assert(!std::is_copy_constructible_v<DocumentView>);
static_assert(!std::is_copy_assignable_v<DocumentView>);

int main() {
    return 0;
}
