#include <HolonightFileBrowser/natural_order.h>
namespace HolonightFileBrowser {
namespace {
bool asciiDigit(QChar character) { return character >= u'0' && character <= u'9'; }
QStringView digitRun(QStringView text, qsizetype& position) {
  auto begin = position;
  while (position < text.size() && asciiDigit(text[position])) {
    ++position;
  }
  while (begin < position && text[begin] == u'0') {
    ++begin;
  }
  return text.mid(begin, position - begin);
}
}  // namespace

bool naturalFileNameLess(const QString& left, const QString& right) {
  const auto foldedLeft = left.toCaseFolded();
  const auto foldedRight = right.toCaseFolded();
  qsizetype leftPosition = 0;
  qsizetype rightPosition = 0;
  while (leftPosition < foldedLeft.size() && rightPosition < foldedRight.size()) {
    if (asciiDigit(foldedLeft[leftPosition]) && asciiDigit(foldedRight[rightPosition])) {
      const auto leftDigits = digitRun(foldedLeft, leftPosition);
      const auto rightDigits = digitRun(foldedRight, rightPosition);
      if (leftDigits.size() != rightDigits.size()) {
        return leftDigits.size() < rightDigits.size();
      }
      const auto comparison = leftDigits.compare(rightDigits);
      if (comparison != 0) {
        return comparison < 0;
      }
      continue;
    }
    if (foldedLeft[leftPosition] != foldedRight[rightPosition]) {
      return foldedLeft[leftPosition] < foldedRight[rightPosition];
    }
    ++leftPosition;
    ++rightPosition;
  }
  if (leftPosition != foldedLeft.size() || rightPosition != foldedRight.size()) {
    return leftPosition == foldedLeft.size();
  }
  return left < right;
}

}  // namespace HolonightFileBrowser
