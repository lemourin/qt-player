#include <QGuiApplication>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickView>

#include "MpvPlayer.h"
#include "ThumbnailGenerator.h"

int main(int argc, char *argv[]) {
  QCoreApplication::setAttribute(Qt::AA_EnableHighDpiScaling);

  QGuiApplication app(argc, argv);

  QIcon::setThemeName("material");

  setlocale(LC_NUMERIC, "C");

  qmlRegisterType<MpvPlayer>("mpv", 1, 0, "MpvPlayer");

  QQmlApplicationEngine engine;
  engine.addImageProvider("thumbnailer", new ThumbnailGenerator);
  engine.load(QUrl(QStringLiteral("qrc:/main.qml")));

  return app.exec();
}
