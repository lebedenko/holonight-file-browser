#pragma once
#include <algorithm>
#include <cstdint>
namespace HolonightFileBrowser {
enum class GridMove : std::uint8_t { Previous, Next, RowUp, RowDown, PageUp, PageDown, First, Last };
constexpr int gridTarget(int current, GridMove move, int count, int columns, int visibleRows) {
  if (count <= 0) {
    return -1;
  }
  const int last = count - 1;
  const int width = std::max(columns, 1);
  const int from = (current < 0 || current > last) ? 0 : current;
  const int page = std::max(1, std::max(visibleRows, 0) / 2) * width;
  switch (move) {
    case GridMove::First:
      return 0;
    case GridMove::Last:
      return last;
    case GridMove::Previous:
      return std::max(from - 1, 0);
    case GridMove::Next:
      return std::min(from + 1, last);
    case GridMove::RowUp:
      return from / width == 0 ? from : from - width;
    case GridMove::RowDown:
      return from / width == last / width ? from : std::min(from + width, last);
    case GridMove::PageUp:
      return std::max(from - page, 0);
    case GridMove::PageDown:
      return std::min(from + page, last);
  }
  return from;
}
}  // namespace HolonightFileBrowser
