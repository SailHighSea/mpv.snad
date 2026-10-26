#include "MainWindow.h"
#include "MpvWidget.h"
#include "SettingsDialog.h"
#include "SkinManager.h"
#include <QCloseEvent>
#include <QCoreApplication>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QListWidget>
#include <QMenuBar>
#include <QMimeData>
#include <QSettings>
#include <QTimer>

MainWindow::MainWindow()
{
    setWindowTitle("mpv.snad");
    resize(1280, 720);
    setAcceptDrops(true);

    mpv_ = new MpvWidget(QCoreApplication::applicationDirPath() + "/portable_config");
    setCentralWidget(mpv_);

    autosave_ = new QTimer(this);
    autosave_->setInterval(30000);
    connect(autosave_, &QTimer::timeout, this, [this] { mpv_->command({"write-watch-later-config"}); });

    buildMenus();
    buildPlaylistDock();
    applySettings();

    const QStringList args = QCoreApplication::arguments();
    if (args.size() > 1) mpv_->loadFile(args.at(1));
}

void MainWindow::buildMenus()
{
    QMenu *file = menuBar()->addMenu("&File");
    file->addAction("&Open File...", this, [this] {
        QString f = QFileDialog::getOpenFileName(this, "Open media");
        if (!f.isEmpty()) mpv_->loadFile(f);
    }, QKeySequence::Open);
    file->addAction("Open &URL...", this, [this] {
        QString u = QInputDialog::getText(this, "Open URL", "URL:");
        if (!u.isEmpty()) mpv_->loadFile(u);
    });
    file->addSeparator();
    file->addAction("&Settings...", this, [this] {
        SettingsDialog dlg(this);
        connect(&dlg, &SettingsDialog::applied, this, &MainWindow::applySettings);
        dlg.exec();
    });
    file->addAction("E&xit", this, &QWidget::close, QKeySequence::Quit);

    // Video: aspect, upscalers (same commands as input.conf Ctrl+A/N/Z), reset
    QMenu *video = menuBar()->addMenu("&Video");
    QMenu *aspect = video->addMenu("Aspect ratio");
    const QList<QPair<QString, QString>> ratios = {
        {"Default", "-1"}, {"4:3", "4:3"}, {"16:9", "16:9"}, {"16:10", "16:10"}, {"2.35:1", "2.35:1"}};
    for (const auto &r : ratios)
        aspect->addAction(r.first, this, [this, v = r.second] { mpv_->setProperty("video-aspect-override", v); });
    QMenu *up = video->addMenu("Upscaler");
    up->addAction("AMD FidelityFX FSR 1.0", this, [this] {
        mpv_->command({"no-osd", "change-list", "glsl-shaders", "set", "~~/shaders/FSR.glsl"});
    });
    up->addAction("NVIDIA Image Scaling (NIS)", this, [this] {
        mpv_->command({"no-osd", "change-list", "glsl-shaders", "set", "~~/shaders/NVScaler.glsl"});
    });
    up->addAction("Off", this, [this] { mpv_->command({"no-osd", "change-list", "glsl-shaders", "clr", ""}); });
    video->addAction("Reset zoom / pan / aspect", this, [this] {
        for (const char *p : {"video-zoom", "video-pan-x", "video-pan-y", "video-aspect-override"})
            mpv_->setProperty(p, QString(p) == "video-aspect-override" ? "-1" : "0");
    });
    QMenu *vtracks = video->addMenu("Video track");
    connect(vtracks, &QMenu::aboutToShow, this, [this, vtracks] { fillTrackMenu(vtracks, "video", "vid"); });

    QMenu *audio = menuBar()->addMenu("&Audio");
    QMenu *atracks = audio->addMenu("Track");
    connect(atracks, &QMenu::aboutToShow, this, [this, atracks] { fillTrackMenu(atracks, "audio", "aid"); });
    audio->addAction("Delay +0.1s", this, [this] { mpv_->command({"add", "audio-delay", "0.1"}); });
    audio->addAction("Delay -0.1s", this, [this] { mpv_->command({"add", "audio-delay", "-0.1"}); });

    QMenu *sub = menuBar()->addMenu("&Subtitles");
    QMenu *stracks = sub->addMenu("Track");
    connect(stracks, &QMenu::aboutToShow, this, [this, stracks] { fillTrackMenu(stracks, "sub", "sid"); });
    sub->addAction("Load file...", this, [this] {
        QString f = QFileDialog::getOpenFileName(this, "Load subtitle");
        if (!f.isEmpty()) mpv_->command({"sub-add", f});
    });
    sub->addAction("Delay +0.1s", this, [this] { mpv_->command({"add", "sub-delay", "0.1"}); });
    sub->addAction("Delay -0.1s", this, [this] { mpv_->command({"add", "sub-delay", "-0.1"}); });
}

void MainWindow::fillTrackMenu(QMenu *menu, const QString &type, const QString &prop)
{
    menu->clear();
    if (type != "video")
        menu->addAction("Off", this, [this, prop] { mpv_->setProperty(prop, "no"); });
    for (const QVariant &v : mpv_->getProperty("track-list").toList()) {
        const QVariantMap t = v.toMap();
        if (t.value("type").toString() != type) continue;
        const QString id = t.value("id").toString();
        QStringList parts{"#" + id, t.value("lang").toString(), t.value("title").toString()};
        parts.removeAll(QString());
        QAction *a = menu->addAction(parts.join("  "), this, [this, prop, id] { mpv_->setProperty(prop, id); });
        a->setCheckable(true);
        a->setChecked(t.value("selected").toBool());
    }
}

void MainWindow::buildPlaylistDock()
{
    auto *dock = new QDockWidget("Playlist", this);
    playlist_ = new QListWidget;
    dock->setWidget(playlist_);
    addDockWidget(Qt::RightDockWidgetArea, dock);
    dock->hide();
    menuBar()->addMenu("&View")->addAction(dock->toggleViewAction());

    connect(playlist_, &QListWidget::itemActivated, this, [this](QListWidgetItem *) {
        mpv_->command({"playlist-play-index", QString::number(playlist_->currentRow())});
    });
    connect(playlist_, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem *) {
        mpv_->command({"playlist-play-index", QString::number(playlist_->currentRow())});
    });
    auto *remove = new QAction("Remove", playlist_);
    remove->setShortcut(Qt::Key_Delete);
    remove->setShortcutContext(Qt::WidgetShortcut);
    playlist_->addAction(remove);
    connect(remove, &QAction::triggered, this, [this] {
        if (playlist_->currentRow() >= 0)
            mpv_->command({"playlist-remove", QString::number(playlist_->currentRow())});
    });

    connect(mpv_, &MpvWidget::playlistChanged, this, &MainWindow::refreshPlaylist);
}

void MainWindow::refreshPlaylist()
{
    playlist_->clear();
    for (const QVariant &v : mpv_->getProperty("playlist").toList()) {
        const QVariantMap e = v.toMap();
        QString name = e.value("title").toString();
        if (name.isEmpty()) name = QFileInfo(e.value("filename").toString()).fileName();
        auto *item = new QListWidgetItem(name, playlist_);
        if (e.value("current").toBool()) {
            QFont f = item->font();
            f.setBold(true);
            item->setFont(f);
        }
    }
}

void MainWindow::applySettings()
{
    QSettings s(SettingsDialog::iniPath(), QSettings::IniFormat);
    SkinManager::apply(s.value("skin", "dark").toString());
    // Only override mpv.conf when the user picked a value in the GUI.
    QString hw = s.value("hwdec").toString();
    if (!hw.isEmpty()) mpv_->setProperty("hwdec", hw);
    mpv_->setProperty("volume", s.value("volume", 100).toString());

    resume_ = s.value("resume", false).toBool();
    mpv_->setProperty("save-position-on-quit", resume_ ? "yes" : "no");
    if (resume_) autosave_->start(); else autosave_->stop();

    const QString q = s.value("quality").toString();
    if (q == "best")
        mpv_->setProperty("ytdl-format", "bestvideo+bestaudio/best");
    else if (!q.isEmpty())
        mpv_->setProperty("ytdl-format", QString("bestvideo[height<=?%1]+bestaudio/best[height<=?%1]").arg(q));
}

void MainWindow::closeEvent(QCloseEvent *e)
{
    if (resume_) mpv_->commandSync({"write-watch-later-config"});
    QMainWindow::closeEvent(e);
}

void MainWindow::dragEnterEvent(QDragEnterEvent *e)
{
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent *e)
{
    for (const QUrl &u : e->mimeData()->urls())
        mpv_->command({"loadfile", u.isLocalFile() ? u.toLocalFile() : u.toString(), "append-play"});
}
