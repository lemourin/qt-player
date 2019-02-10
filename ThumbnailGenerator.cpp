#include "ThumbnailGenerator.h"

#include <memory>
#include <mutex>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

#include <QDebug>
#include <QThreadPool>

const int THUMBNAIL_SIZE = 256;

namespace {

std::mutex mutex;
bool initialized;

struct ImageSize {
  int width_;
  int height_;
};

struct CallbackData {
  std::function<bool(std::chrono::system_clock::time_point)> interrupt_;
  std::chrono::system_clock::time_point start_time_;
};

template <class T>
using Pointer = std::unique_ptr<T, std::function<void(T*)>>;

template <class T>
Pointer<T> make(T* d, std::function<void(T**)> f) {
  return Pointer<T>(d, [=](T* d) { f(&d); });
}

template <class T>
Pointer<T> make(T* d, std::function<void(T*)> f) {
  return Pointer<T>(d, f);
}

std::string av_error(int err) {
  char buffer[AV_ERROR_MAX_STRING_SIZE + 1] = {};
  if (av_strerror(err, buffer, AV_ERROR_MAX_STRING_SIZE) < 0)
    return "invalid error";
  else
    return buffer;
}

void check(int code, const std::string& call) {
  if (code < 0) throw std::logic_error(call + " (" + av_error(code) + ")");
}

void initialize() {
  std::unique_lock<std::mutex> lock(mutex);
  if (!initialized) {
#if LIBAVFORMAT_VERSION_INT < AV_VERSION_INT(58, 9, 100)
    av_register_all();
    avfilter_register_all();
#endif
    av_log_set_level(AV_LOG_PANIC);
    check(avformat_network_init(), "avformat_network_init");
#if LIBAVCODEC_VERSION_INT < AV_VERSION_INT(58, 9, 100)
    check(av_lockmgr_register([](void** data, AVLockOp op) {
            if (op == AV_LOCK_CREATE)
              *data = new std::mutex;
            else if (op == AV_LOCK_DESTROY)
              delete static_cast<std::mutex*>(*data);
            else if (op == AV_LOCK_OBTAIN)
              static_cast<std::mutex*>(*data)->lock();
            else if (op == AV_LOCK_RELEASE)
              static_cast<std::mutex*>(*data)->unlock();
            return 0;
          }),
          "av_lockmgr_register");
#endif
    initialized = true;
  }
}

Pointer<AVFormatContext> create_format_context(
    const std::string& url,
    std::function<bool(std::chrono::system_clock::time_point)> interrupt) {
  auto context = avformat_alloc_context();
  auto data = new CallbackData{interrupt, std::chrono::system_clock::now()};
  context->interrupt_callback.opaque = data;
  context->interrupt_callback.callback = [](void* t) -> int {
    auto d = reinterpret_cast<CallbackData*>(t);
    return d->interrupt_(d->start_time_);
  };
  int e = 0;
  if ((e = avformat_open_input(&context, url.c_str(), nullptr, nullptr)) < 0) {
    avformat_free_context(context);
    delete data;
    check(e, "avformat_open_input");
  } else if ((e = avformat_find_stream_info(context, nullptr)) < 0) {
    avformat_close_input(&context);
    delete data;
    check(e, "avformat_find_stream_info");
  }
  return make<AVFormatContext>(context, [data](AVFormatContext* d) {
    avformat_close_input(&d);
    delete data;
  });
}

Pointer<AVCodecContext> create_codec_context(AVFormatContext* context,
                                             int stream_index) {
  auto codec =
      avcodec_find_decoder(context->streams[stream_index]->codecpar->codec_id);
  if (!codec) throw std::logic_error("decoder not found");
  auto codec_context =
      make<AVCodecContext>(avcodec_alloc_context3(codec), avcodec_free_context);
  check(avcodec_parameters_to_context(codec_context.get(),
                                      context->streams[stream_index]->codecpar),
        "avcodec_parameters_to_context");
  check(avcodec_open2(codec_context.get(), codec, nullptr), "avcodec_open2");
  return codec_context;
}

Pointer<AVPacket> create_packet() {
  auto packet = new AVPacket;
  av_init_packet(packet);
  packet->data = nullptr;
  packet->size = 0;
  return Pointer<AVPacket>(packet, [](AVPacket* packet) {
    av_packet_unref(packet);
    delete packet;
  });
}

Pointer<AVFrame> decode_frame(AVFormatContext* context,
                              AVCodecContext* codec_context, int stream_index) {
  Pointer<AVFrame> result_frame;
  auto packet = create_packet();
  while (!result_frame) {
    auto read_packet = av_read_frame(context, packet.get());
    if (read_packet != 0 && read_packet != AVERROR_EOF) {
      check(read_packet, "av_read_frame");
    } else {
      if (read_packet == 0 && packet->stream_index != stream_index) continue;
      auto send_packet = avcodec_send_packet(
          codec_context, read_packet == AVERROR_EOF ? nullptr : packet.get());
      if (send_packet != AVERROR_EOF) check(send_packet, "avcodec_send_packet");
    }
    auto frame = make<AVFrame>(av_frame_alloc(), av_frame_free);
    auto code = avcodec_receive_frame(codec_context, frame.get());
    if (code == 0) {
      result_frame = std::move(frame);
    } else if (code == AVERROR_EOF) {
      break;
    } else if (code != AVERROR(EAGAIN)) {
      check(code, "avcodec_receive_frame");
    }
  }
  return result_frame;
}

std::string encode_frame(AVFrame* frame) {
  auto png_codec = avcodec_find_encoder(AV_CODEC_ID_PNG);
  if (!png_codec) throw std::logic_error("png codec not found");
  auto png_context = make<AVCodecContext>(avcodec_alloc_context3(png_codec),
                                          avcodec_free_context);
  png_context->time_base = {1, 24};
  png_context->pix_fmt = AVPixelFormat(frame->format);
  png_context->width = frame->width;
  png_context->height = frame->height;
  check(avcodec_open2(png_context.get(), png_codec, nullptr), "avcodec_open2");
  auto packet = create_packet();
  bool frame_sent = false, flush_sent = false;
  std::string result;
  while (true) {
    if (!frame_sent) {
      check(avcodec_send_frame(png_context.get(), frame), "avcodec_send_frame");
      frame_sent = true;
    } else if (!flush_sent) {
      check(avcodec_send_frame(png_context.get(), nullptr),
            "avcodec_send_frame");
      flush_sent = true;
    }
    auto err = avcodec_receive_packet(png_context.get(), packet.get());
    if (err != 0) {
      if (err == AVERROR_EOF)
        break;
      else
        check(err, "avcodec_receive_packet");
    } else {
      result +=
          std::string(reinterpret_cast<char*>(packet->data), packet->size);
    }
  }
  return result;
}

Pointer<AVFrame> create_rgb_frame(AVFrame* frame, ImageSize size) {
  auto format = AV_PIX_FMT_RGBA;
  auto sws_context = make<SwsContext>(
      sws_getContext(frame->width, frame->height, AVPixelFormat(frame->format),
                     size.width_, size.height_, format, SWS_BICUBIC, nullptr,
                     nullptr, nullptr),
      sws_freeContext);
  auto rgb_frame = make<AVFrame>(av_frame_alloc(), av_frame_free);
  av_frame_copy_props(rgb_frame.get(), frame);
  rgb_frame->format = format;
  rgb_frame->width = size.width_;
  rgb_frame->height = size.height_;
  check(av_image_alloc(rgb_frame->data, rgb_frame->linesize, size.width_,
                       size.height_, format, 32),
        "av_image_alloc");
  check(sws_scale(sws_context.get(), frame->data, frame->linesize, 0,
                  frame->height, rgb_frame->data, rgb_frame->linesize),
        "sws_scale");
  return make<AVFrame>(rgb_frame.release(), [=](AVFrame* f) {
    av_freep(&f->data);
    av_frame_free(&f);
  });
}

ImageSize thumbnail_size(const ImageSize& i, int target) {
  if (i.width_ > i.height_) {
    return {target, i.height_ * target / i.width_};
  } else {
    return {i.width_ * target / i.height_, target};
  }
}

std::string generate_thumbnail(
    const std::string& url, int64_t timestamp,
    std::function<bool(std::chrono::system_clock::time_point)> interrupt) {
  initialize();
  std::string effective_url = url;
#ifdef _WIN32
  const char* file = "file:///";
#else
  const char* file = "file://";
#endif
  const auto length = strlen(file);
  if (url.substr(0, length) == file) effective_url = url.substr(length);
  auto context = create_format_context(effective_url, interrupt);
  auto stream = av_find_best_stream(context.get(), AVMEDIA_TYPE_VIDEO, -1, -1,
                                    nullptr, 0);
  check(stream, "av_find_best_stream");
  check(avformat_seek_file(context.get(), -1, INT64_MIN,
                           timestamp * AV_TIME_BASE / 1000, INT64_MAX, 0),
        "avformat_seek_file");
  auto codec_context = create_codec_context(context.get(), stream);
  auto size = thumbnail_size({codec_context->width, codec_context->height},
                             THUMBNAIL_SIZE);
  Pointer<AVFrame> current =
      decode_frame(context.get(), codec_context.get(), stream);
  if (!current) throw std::logic_error("couldn't get frame");
  auto rgb_frame = create_rgb_frame(current.get(), size);
  return encode_frame(rgb_frame.get());
}

class Generator : public QQuickImageResponse {
 public:
  Generator(const std::string& url, int timestamp)
      : task_(new Runnable(this, url, timestamp)) {
    task_->setAutoDelete(false);
    QThreadPool::globalInstance()->start(task_);
  }

  ~Generator() override { delete task_; }

  void cancel() override { task_->cancelled_ = true; }

  QString errorString() const override { return error_; }

  QQuickTextureFactory* textureFactory() const override {
    return QQuickTextureFactory::textureFactoryForImage(image_);
  }

 private:
  class Runnable : public QRunnable {
   public:
    Runnable(Generator* r, const std::string& url, int timestamp)
        : response_(r), url_(url), timestamp_(timestamp), cancelled_(false) {}

    void run() override {
      try {
        auto data = generate_thumbnail(
            url_, timestamp_, [this](std::chrono::system_clock::time_point) {
              return bool(cancelled_);
            });
        response_->image_ = QImage::fromData(
            reinterpret_cast<const uchar*>(data.c_str()), data.size());
        emit response_->finished();
      } catch (const std::exception& e) {
        response_->error_ = e.what();
        emit response_->finished();
      }
    }

    std::atomic_bool cancelled_;

   private:
    Generator* response_;
    std::string url_;
    int timestamp_;
  };

  Runnable* task_;
  QString error_;
  QImage image_;
};

}  // namespace

QQuickImageResponse* ThumbnailGenerator::requestImageResponse(
    const QString& id, const QSize& requested_size) {
  auto timestamp = id.left(id.indexOf('/')).toLongLong() * 10000;
  auto url = id.right(id.length() - id.indexOf('/') - 1);
  return new Generator(url.toStdString(), timestamp);
}
