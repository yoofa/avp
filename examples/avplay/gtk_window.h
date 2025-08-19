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

#include "media/foundation/media_frame.h"
#include "media/video/video_render.h"

using ave::media::MediaFrame;
using ave::media::VideoRender;

class GtkWnd {
 protected:
  class GtkVideoRender : public VideoRender {
   public:
    GtkVideoRender(GtkWnd* window);
    ~GtkVideoRender() override;

    void OnFrame(const std::shared_ptr<MediaFrame>& frame) override;
    const uint8_t* image() const { return image_.get(); }

    int width() const { return width_; }
    int height() const { return height_; }

   private:
    void setSize(int width, int height);
    GtkWnd* gtkwnd_;
    std::unique_ptr<uint8_t[]> image_;
    int width_;
    int height_;
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

  std::shared_ptr<GtkVideoRender> videoRender() { return video_render_; }

 private:
  GtkWidget* window_;
  GtkWidget* draw_area_;
  std::shared_ptr<GtkVideoRender> video_render_;
  int width_;
  int height_;
  std::unique_ptr<uint8_t[]> draw_buffer_;
  int draw_buffer_size_;
};

#endif /* !GTK_WINDOW_H */
