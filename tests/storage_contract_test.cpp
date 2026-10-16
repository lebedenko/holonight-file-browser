#include <QFile>
#include <QRegularExpression>

#include <gtest/gtest.h>
TEST(FilesStorage, CapacityTextIsATranslatableTwoArgumentFormat) {
  QFile source(QStringLiteral(BROWSER_SOURCE_DIR "/src/storage/devices_model.cpp"));
  ASSERT_TRUE(source.open(QIODevice::ReadOnly));
  EXPECT_TRUE(source.readAll().contains(R"(tr("%1 free of %2"))"));
}

TEST(StoragePolicy, SourceReadsNoHardwareSpecificIdentifiers) {
  QFile file(QStringLiteral(BROWSER_SOURCE_DIR "/include/HolonightFileBrowser/storage/storage_policy.h"));
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  const auto source = QString::fromUtf8(file.readAll());
  const QRegularExpression forbidden(
      R"re(\b(vendor|model|serial|siblingId|device|mountPoints)\b|"(usb|ieee1394|sdio)")re");
  const auto match = forbidden.match(source);
  EXPECT_FALSE(match.hasMatch()) << match.captured().toStdString();
}
