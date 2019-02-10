#ifndef THUMBNAILGENERATOR_H
#define THUMBNAILGENERATOR_H

#include <QQuickAsyncImageProvider>

class ThumbnailGenerator : public QQuickAsyncImageProvider {
 public:
  QQuickImageResponse* requestImageResponse(
      const QString& id, const QSize& requested_size) override;
};

#endif  // THUMBNAILGENERATOR_H
