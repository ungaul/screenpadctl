#pragma once
#include <QAbstractListModel>
#include <QString>
#include <QStringList>
#include <vector>

class Hyprland;

class DesktopModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { NameRole = Qt::UserRole + 1, IconRole, KindRole };

    explicit DesktopModel(QStringList dirs, QObject* parent = nullptr);

    int rowCount(const QModelIndex& = {}) const override { return static_cast<int>(items_.size()); }
    QVariant data(const QModelIndex& idx, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void reload();
    Q_INVOKABLE void launch(int row);

    void setHyprland(Hyprland* h) { hypr_ = h; }

private:
    struct Item {
        QString name;
        QString icon;
        QString kind;
        QString target;
        QString exec;
        QString workDir;
    };
    static bool parseDesktop(const QString& path, Item& out);
    static QString stripFieldCodes(const QString& exec);

    void run(const QString& shellCmd, const QString& workDir) const;

    Hyprland* hypr_ = nullptr;
    QStringList dirs_;
    std::vector<Item> items_;
};
