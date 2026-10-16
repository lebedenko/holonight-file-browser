#include <QCoreApplication>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>
int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir cache;
  if (!cache.isValid() || !qputenv("XDG_CACHE_HOME", QFile::encodeName(cache.path()))) {
    return 1;
  }
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
