/*
 * gtk_window.h
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef GTK_WINDOW_H
#define GTK_WINDOW_H

#include <memory>

#include <gtk/gtk.h>

#include "common/buffer.h"
#include "common/message.h"
#include "player/video_sink.h"

class GtkWnd {
 protected:
  class GtkVideoRender : public avp::VideoSink {
   public:
    GtkVideoRender(GtkWnd* window);
    virtual ~GtkVideoRender();
    void onFrame(std::shared_ptr<avp::Buffer>& frame) override;
    const uint8_t* image() const { return mImage.get(); }

    int width() const { return mWidth; }
    int height() const { return mHeight; }

   private:
    void setSize(int width, int height);
    GtkWnd* mGtkWnd;
    std::unique_ptr<uint8_t[]> mImage;
    int mWidth;
    int mHeight;
  };

 public:
  GtkWnd();
  virtual ~GtkWnd();
  bool create();
  bool destroy();
  void addVideoRender();

  void onDestroyed(GtkWidget* widget);
  void onKeyPress(GtkWidget* widget, GdkEventKey* key);
  void onClicked(GtkWidget* widget);
  void onRedraw();

  void draw(GtkWidget* widget, cairo_t* cr);

  std::shared_ptr<GtkVideoRender> videoRender() { return mVideoRender; }

 private:
  GtkWidget* mWindow;
  GtkWidget* mDrawArea;
  std::shared_ptr<GtkVideoRender> mVideoRender;
  int mWidth;
  int mHeight;
  std::unique_ptr<uint8_t[]> mDrawBuffer;
  int mDrawBufferSize;
};

#endif /* !GTK_WINDOW_H */
