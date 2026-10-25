#pragma once

#include <Qt>

#include <cstdint>

struct DirectoryRoles {
  // Qt model roles/QML properties require implicit integer conversion.
  // NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
  enum Role : std::uint16_t {
    NameRole = Qt::UserRole + 1,
    PathRole = Qt::UserRole + 2,
    IsDirRole = Qt::UserRole + 3,
    SizeRole = Qt::UserRole + 4,
    ModifiedRole = Qt::UserRole + 5,
    ModeRole = Qt::UserRole + 6,
    IsHiddenRole = Qt::UserRole + 7,
    StatFailedRole = Qt::UserRole + 8,
    StatErrorRole = Qt::UserRole + 9,
    // SPEC.md REQ-C-001 (main-view-icons): the '/'-joined icon-name candidate chain, consumed from
    // QML only as "image://icon/" + iconName and split apart only by IconImageProvider.
    IconNameRole = Qt::UserRole + 10,
    // Qt::UserRole + 11 was the removed parent role; keep subsequent values stable.
    IsSymlinkRole = Qt::UserRole + 12,
    NativePathRole = Qt::UserRole + 13,
  };
};
