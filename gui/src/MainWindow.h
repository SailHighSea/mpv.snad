#pragma once
#include <QMainWindow>

class MpvWidget;
class QListWidget;
class QMenu;
class QTimer;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    MainWindow();

protected:
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dropEvent(QDropEvent *e) override;
    void closeEvent(QCloseEvent *e) override;

private:
    void applySettings();
    void buildMenus();
    void buildPlaylistDock();
    void refreshPlaylist();
    void fillTrackMenu(QMenu *menu, const QString &type, const QString &prop);

    MpvWidget *mpv_;
    QListWidget *playlist_ = nullptr;
    QTimer *autosave_ = nullptr;
    bool resume_ = false;
};
