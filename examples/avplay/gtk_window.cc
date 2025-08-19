/*
 * gtk_window.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "gtk_window.h"

#include <gtk/gtk.h>
#include <memory>

#include "base/checks.h"
#include "base/logging.h"
#include "third_party/libyuv/include/libyuv/convert.h"
#include "third_party/libyuv/include/libyuv/convert_from.h"

namespace {

gboolean OnDestroyedCallback(GtkWidget* widget,
                             GdkEvent* event,
                             gpointer data) {
  reinterpret_cast<GtkWnd*>(data)->onDestroyed(widget);
  return FALSE;
}

gboolean OnKeyPressCallback(GtkWidget* widget,
                            GdkEventKey* key,
                            gpointer data) {
  reinterpret_cast<GtkWnd*>(data)->onKeyPress(widget, key);
  return false;
}

// void OnClickedCallback(GtkWidget* widget, gpointer data) {
//  reinterpret_cast<GtkWnd*>(data)->onClicked(widget);
//}

gboolean Redraw(gpointer data) {
  GtkWnd* wnd = reinterpret_cast<GtkWnd*>(data);
  wnd->onRedraw();
  return false;
}

gboolean Draw(GtkWidget* widget, cairo_t* cr, gpointer data) {
  GtkWnd* wnd = reinterpret_cast<GtkWnd*>(data);
  wnd->draw(widget, cr);
  return false;
}
}  // namespace

GtkWnd::GtkWnd()
    : window_(nullptr), draw_area_(nullptr), video_render_(nullptr) {}

GtkWnd::~GtkWnd() {}

bool GtkWnd::create() {
  window_ = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  if (window_ != nullptr) {
    gtk_window_set_position(GTK_WINDOW(window_), GTK_WIN_POS_CENTER);
    gtk_window_set_default_size(GTK_WINDOW(window_), 640, 480);
    gtk_window_set_title(GTK_WINDOW(window_), "avplayer demo");
    g_signal_connect(G_OBJECT(window_), "delete-event",
                     G_CALLBACK(&OnDestroyedCallback), this);
    g_signal_connect(G_OBJECT(window_), "key-press-event",
                     G_CALLBACK(OnKeyPressCallback), this);

    gtk_widget_show(window_);
  }

  return window_ != nullptr;
}

void GtkWnd::addVideoRender() {
  AVE_LOG(LS_INFO) << "addVideoRender";
  gtk_container_set_border_width(GTK_CONTAINER(window_), 0);
  draw_area_ = gtk_drawing_area_new();
  gtk_container_add(GTK_CONTAINER(window_), draw_area_);
  g_signal_connect(G_OBJECT(draw_area_), "draw", G_CALLBACK(Draw), this);
  gtk_widget_show_all(window_);
  video_render_.reset(new GtkVideoRender(this));
}

void GtkWnd::onDestroyed(GtkWidget* widget) {
  AVE_LOG(ave::LS_INFO) << "onDestroyed";
}

void GtkWnd::onKeyPress(GtkWidget* widget, GdkEventKey* key) {
  AVE_LOG(ave::LS_INFO) << "onKeyPress, type:" << key->type;
}

void GtkWnd::onClicked(GtkWidget* widget) {}

void GtkWnd::onRedraw() {
  GDK_THREADS_ENTER();

  if (video_render_ != nullptr && video_render_->image() != nullptr &&
      draw_area_ != nullptr) {
    width_ = video_render_->width();
    height_ = video_render_->height();
    if (!draw_buffer_) {
      draw_buffer_size_ = (width_ * height_ * 4) * 4;
      draw_buffer_.reset(new uint8_t[draw_buffer_size_]);
      gtk_widget_set_size_request(draw_area_, width_ * 2, height_ * 2);
    }

    const auto* image =
        reinterpret_cast<const uint32_t*>(video_render_->image());
    auto* scaled = reinterpret_cast<uint32_t*>(draw_buffer_.get());
    for (int r = 0; r < height_; ++r) {
      for (int c = 0; c < width_; ++c) {
        int x = c * 2;
        scaled[x] = scaled[x + 1] = image[c];
      }

      uint32_t* prev_line = scaled;
      scaled += width_ * 2;
      memcpy(scaled, prev_line, (width_ * 2) * 4);

      image += width_;
      scaled += width_ * 2;
    }

#if GTK_MAJOR_VERSION == 2
    gdk_draw_rgb_32_image(draw_area_->window,
                          draw_area_->style->fg_gc[GTK_STATE_NORMAL], 0, 0,
                          width_ * 2, height_ * 2, GDK_RGB_DITHER_MAX,
                          draw_buffer_.get(), (width_ * 2) * 4);
#else
    gtk_widget_queue_draw(draw_area_);
#endif
  }
  GDK_THREADS_LEAVE();
}

void GtkWnd::draw(GtkWidget* widget, cairo_t* cr) {
#if GTK_MAJOR_VERSION != 2
  cairo_format_t format = CAIRO_FORMAT_ARGB32;
  cairo_surface_t* surface = cairo_image_surface_create_for_data(
      draw_buffer_.get(), format, width_ * 2, height_ * 2,
      cairo_format_stride_for_width(format, width_ * 2));
  cairo_set_source_surface(cr, surface, 0, 0);
  cairo_rectangle(cr, 0, 0, width_ * 2, height_ * 2);
  cairo_fill(cr);
  cairo_surface_destroy(surface);
#else
  RTC_NOTREACHED();
#endif
}

GtkWnd::GtkVideoRender::GtkVideoRender(GtkWnd* window)
    : gtkwnd_(window), image_(nullptr), width_(0), height_(0) {}

GtkWnd::GtkVideoRender::~GtkVideoRender() {}

void GtkWnd::GtkVideoRender::OnFrame(const std::shared_ptr<MediaFrame>& frame) {
  int32_t width = frame->width();
  int32_t height = frame->height();
  int32_t stride = frame->stride();

  setSize(width, height);

  libyuv::I420ToARGB(frame->data(), stride, frame->data() + stride * height,
                     stride / 2, frame->data() + stride * height * 5 / 4,
                     stride / 2, image_.get(), width_ * 4, width, height);

  g_idle_add(Redraw, gtkwnd_);
}

void GtkWnd::GtkVideoRender::setSize(int width, int height) {
  GDK_THREADS_ENTER();
  if (width_ == width && height_ == height) {
    return;
  }

  width_ = width;
  height_ = height;
  image_.reset(new uint8_t[width * height * 4]);
  GDK_THREADS_LEAVE();
}
