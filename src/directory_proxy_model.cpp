#include <HolonightFileBrowser/directory_proxy_model.h>
#include <HolonightFileBrowser/directory_roles.h>

DirectoryProxyModel::DirectoryProxyModel(QObject* parent) : QSortFilterProxyModel(parent) {
  if (collator_.locale().language() == QLocale::C) {
    collator_.setLocale(QLocale(QLocale::English, QLocale::UnitedStates));
  }
  collator_.setNumericMode(true);
  collator_.setCaseSensitivity(Qt::CaseInsensitive);
  setDynamicSortFilter(true);
  // Establishes the initial sort; without one explicit call, dynamicSortFilter has nothing to
  // reapply as new rows land, and rows would stay in walk-discovery order.
  sort(0, sort_order_);
}
void DirectoryProxyModel::setHiddenVisible(bool visible) {
  if (hidden_visible_ == visible) {
    return;
  }
  hidden_visible_ = visible;
  beginFilterChange();
  endFilterChange();
  emit hiddenVisibleChanged();
}
void DirectoryProxyModel::setSortDescending(bool descending) {
  const auto order = descending ? Qt::DescendingOrder : Qt::AscendingOrder;
  if (sort_order_ == order) {
    return;
  }
  sort_order_ = order;
  sort(0, sort_order_);
  emit sortDescendingChanged();
}
// Qt's descending mode reverses whatever lessThan() decides, including the directories-first
// grouping below; REQ-F-003 only mandates dirs-first "by default" (ascending), so this is accepted.
bool DirectoryProxyModel::lessThan(const QModelIndex& left, const QModelIndex& right) const {
  const bool leftIsParent = sourceModel()->data(left, DirectoryRoles::IsParentRole).toBool();
  const bool rightIsParent = sourceModel()->data(right, DirectoryRoles::IsParentRole).toBool();
  if (leftIsParent != rightIsParent) {
    return leftIsParent ? (sort_order_ == Qt::AscendingOrder) : (sort_order_ == Qt::DescendingOrder);
  }
  const bool leftIsDir = sourceModel()->data(left, DirectoryRoles::IsDirRole).toBool();
  const bool rightIsDir = sourceModel()->data(right, DirectoryRoles::IsDirRole).toBool();
  if (leftIsDir != rightIsDir) {
    return leftIsDir;
  }
  const auto leftName = sourceModel()->data(left, DirectoryRoles::NameRole).toString();
  const auto rightName = sourceModel()->data(right, DirectoryRoles::NameRole).toString();
  const int cmp = compareNames(leftName, rightName);
  if (cmp != 0) {
    return cmp < 0;
  }
  return sourceModel()->data(left, DirectoryRoles::NativePathRole).toByteArray() <
         sourceModel()->data(right, DirectoryRoles::NativePathRole).toByteArray();
}
int DirectoryProxyModel::compareNames(const QString& left, const QString& right) const {
  const int comparison = collator_.compare(left, right);
  return comparison == 0 ? QString::compare(left, right, Qt::CaseSensitive) : comparison;
}
bool DirectoryProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
  const auto sourceIndex = sourceModel()->index(sourceRow, 0, sourceParent);
  if (sourceModel()->data(sourceIndex, DirectoryRoles::IsParentRole).toBool()) {
    return true;
  }
  if (hidden_visible_) {
    return true;
  }
  return !sourceModel()->data(sourceIndex, DirectoryRoles::IsHiddenRole).toBool();
}
