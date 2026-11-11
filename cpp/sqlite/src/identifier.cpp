#include "internal.hpp"

namespace lapilli::sqlite::detail {

//ASCII だけを見る (ロケールに左右される isalpha などは使わない)
static bool is_head(char c) {
	return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}

static bool is_tail(char c) {
	return is_head(c) || (c >= '0' && c <= '9');
}

bool is_identifier(std::string_view name) {
	if (name.empty() || name.size() > 128) return false;
	if (!is_head(name[0])) return false;
	for (char c : name) {
		if (!is_tail(c)) return false;
	}
	return true;
}

}
