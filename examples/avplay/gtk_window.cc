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
    : mWindow(nullptr), mDrawArea(nullptr), mVideoRender(nullptr) {}

GtkWnd::~GtkWnd() {}

bool GtkWnd::create() {
  mWindow = gtk_window_new(GTK_WINDOW_TOPLEVEL);

  if (mWindow != nullptr) {
    gtk_window_set_position(GTK_WINDOW(mWindow), GTK_WIN_POS_CENTER);
    gtk_window_set_default_size(GTK_WINDOW(mWindow), 640, 480);
    gtk_window_set_title(GTK_WINDOW(mWindow), "avplayer demo");
    g_signal_connect(G_OBJECT(mWindow), "delete-event",
                     G_CALLBACK(&OnDestroyedCallback), this);
    g_signal_connect(G_OBJECT(mWindow), "key-press-event",
                     G_CALLBACK(OnKeyPressCallback), this);

    gtk_widget_show(mWindow);
  }

  return mWindow != nullptr;
}

void GtkWnd::addVideoRender() {
  gtk_container_set_border_width(GTK_CONTAINER(mWindow), 0);
  mDrawArea = gtk_drawing_area_new();
  gtk_container_add(GTK_CONTAINER(mWindow), mDrawArea);
  g_signal_connect(G_OBJECT(mDrawArea), "draw", G_CALLBACK(Draw), this);
  gtk_widget_show_all(mWindow);
  mVideoRender.reset(new GtkVideoRender(this));
}

void GtkWnd::onDestroyed(GtkWidget* widget) {
  LOG(avp::LS_INFO) << "onDestroyed";
}

void GtkWnd::onKeyPress(GtkWidget* widget, GdkEventKey* key) {
  LOG(avp::LS_INFO) << "onKeyPress, type:" << key->type;
}

void GtkWnd::onClicked(GtkWidget* widget) {}

void GtkWnd::onRedraw() {
  GDK_THREADS_ENTER();

  if (mVideoRender != nullptr && mVideoRender->image() != nullptr &&
      mDrawArea != nullptr) {
    mWidth = mVideoRender->width();
    mHeight = mVideoRender->height();
    if (!mDrawBuffer.get()) {
      mDrawBufferSize = (mWidth * mHeight * 4) * 4;
      mDrawBuffer.reset(new uint8_t[mDrawBufferSize]);
      gtk_widget_set_size_request(mDrawArea, mWidth * 2, mHeight * 2);
    }

    const uint32_t* image =
        reinterpret_cast<const uint32_t*>(mVideoRender->image());
    uint32_t* scaled = reinterpret_cast<uint32_t*>(mDrawBuffer.get());
    for (int r = 0; r < mHeight; ++r) {
      for (int c = 0; c < mWidth; ++c) {
        int x = c * 2;
        scaled[x] = scaled[x + 1] = image[c];
      }

      uint32_t* prev_line = scaled;
      scaled += mWidth * 2;
      memcpy(scaled, prev_line, (mWidth * 2) * 4);

      image += mWidth;
      scaled += mWidth * 2;
    }

#if GTK_MAJOR_VERSION == 2
    gdk_draw_rgb_32_image(mDrawArea->window,
                          mDrawArea->style->fg_gc[GTK_STATE_NORMAL], 0, 0,
                          mWidth * 2, mHeight * 2, GDK_RGB_DITHER_MAX,
                          mDrawBuffer.get(), (mWidth * 2) * 4);
#else
    gtk_widget_queue_draw(mDrawArea);
#endif
  }
  GDK_THREADS_LEAVE();
}

void GtkWnd::draw(GtkWidget* widget, cairo_t* cr) {
#if GTK_MAJOR_VERSION != 2
  cairo_format_t format = CAIRO_FORMAT_ARGB32;
  cairo_surface_t* surface = cairo_image_surface_create_for_data(
      mDrawBuffer.get(), format, mWidth * 2, mHeight * 2,
      cairo_format_stride_for_width(format, mWidth * 2));
  cairo_set_source_surface(cr, surface, 0, 0);
  cairo_rectangle(cr, 0, 0, mWidth * 2, mHeight * 2);
  cairo_fill(cr);
  cairo_surface_destroy(surface);
#else
  RTC_NOTREACHED();
#endif
}

GtkWnd::GtkVideoRender::GtkVideoRender(GtkWnd* window) : mGtkWnd(window) {}

GtkWnd::GtkVideoRender::~GtkVideoRender() {}

void GtkWnd::GtkVideoRender::onFrame(std::shared_ptr<avp::Buffer>& frame) {
  int32_t width;
  CHECK(frame->meta()->findInt32("width", &width));
  int32_t height;
  CHECK(frame->meta()->findInt32("height", &height));
  int32_t stride;
  CHECK(frame->meta()->findInt32("stride", &stride));

  setSize(width, height);

  libyuv::I420ToARGB(frame->data(), stride, frame->data() + stride * height,
                     stride / 2, frame->data() + stride * height * 5 / 4,
                     stride / 2, mImage.get(), mWidth * 4, width, height);

  g_idle_add(Redraw, mGtkWnd);
}

void GtkWnd::GtkVideoRender::setSize(int width, int height) {
  GDK_THREADS_ENTER();
  if (mWidth == width && mHeight == height) {
    return;
  }

  mWidth = width;
  mHeight = height;
  mImage.reset(new uint8_t[width * height * 4]);
  GDK_THREADS_LEAVE();
}
