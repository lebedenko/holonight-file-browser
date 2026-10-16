#pragma once

#include <QRegularExpression>
#include <QStringList>
#include <QVector>

struct SearchExclusions {
  QStringList paths;
  QStringList patterns;
};

// Value snapshot captured by each scan. No filesystem access or mutation while matching.
class SearchExclusionPolicy {
 public:
  static SearchExclusionPolicy compile(const SearchExclusions& settings, QStringList& diagnostics);
  [[nodiscard]] QByteArray fingerprint() const;
  [[nodiscard]] bool excludes(const QString& path, bool directory, const QString& root) const;
  bool operator==(const SearchExclusionPolicy& other) const {
    return paths_ == other.paths_ && patterns_ == other.patterns_;
  }

 private:
  QStringList paths_;
  QStringList patterns_;
  QVector<QRegularExpression> expressions_;
};
