/*
 * av_play.cc
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "av_play.h"

#include <fcntl.h>
#include <getopt.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <iostream>
#include <memory>

#include <gtk/gtk.h>

#include "base/checks.h"
#include "base/logging.h"
#include "examples/AudioFileRender.h"
#include "examples/VideoFileRender.h"
#include "examples/avplay/gtk_window.h"
#include "player/avplayer.h"

using namespace avp;

void printHelp() {
  std::cout << "help:\n"
               "  -f/--file <file>: file to play \n"
               "  -h/--helo\n";
  exit(1);
}

void event_loop() {
  while (true) {
  }
}

std::mutex mMutex;
std::condition_variable mCondition;

class ExListener : public AvPlayer::Listener {
 public:
  ExListener() = default;
  virtual ~ExListener() = default;
  void notify(int what, std::shared_ptr<avp::Message> info) {
    std::cout << "ExListener, what: " << what << ", info: " << info
              << std::endl;
    switch (what) {
      case avp::PlayerBase::kWhatSetDataSourceCompleted: {
        AVE_LOG(ave::LS_INFO) << "kWhatSetDataSourceCompleted";
        break;
      }
      case avp::PlayerBase::kWhatPrepared: {
        AVE_LOG(ave::LS_INFO) << "avplayer prepared";
        mCondition.notify_all();
        break;
      }
      default:
        break;
    }
  }
};

int main(int argc, char* argv[]) {
  gtk_init(&argc, &argv);
  std::string filename;
  std::string url;
  int choice;
  while (1) {
    static struct option long_options[] = {{"help", no_argument, 0, 'h'},
                                           {"file", required_argument, 0, 'f'},
                                           {0, 0, 0, 0}};

    int option_index = 0;

    choice = getopt_long(argc, argv, "hf:", long_options, &option_index);

    if (choice == -1)
      break;

    switch (choice) {
      case 'f': {
        filename = std::string(optarg);

      } break;
      case 'h':
      case '?':
      default:
        printHelp();
        break;
    }
  }
  // avp::LogMessage::LogToDebug(avp::LogSeverity::LS_VERBOSE);
  std::unique_lock<std::mutex> lock(mMutex);

  std::unique_ptr<GtkWnd> gtkWindow(std::make_unique<GtkWnd>());
  gtkWindow->create();

  std::cout << "play file: " << filename << std::endl;
  url = std::string("file://") + filename;

  std::shared_ptr<ExListener> listener(std::make_shared<ExListener>());

  std::shared_ptr<AvPlayer> mPlayer = std::make_shared<AvPlayer>();
  mPlayer->setListener(std::static_pointer_cast<AvPlayer::Listener>(listener));
  mPlayer->init();

  gtkWindow->addVideoRender();
  mPlayer->setVideoSink(gtkWindow->videoRender());

  // std::shared_ptr<VideoFileRender> videoRender =
  //    std::make_unique<VideoFileRender>("local.yuv");
  // std::shared_ptr<AudioFileRender> audioRender =
  //    std::make_unique<AudioFileRender>("local.pcm");
  // mPlayer->setVideoSink(videoRender);
  // mPlayer->setAudioSink(audioRender);

  mPlayer->setDataSource(url.c_str());

  mPlayer->prepare();
  mPlayer->start();

  mCondition.wait(lock);

  mPlayer->start();

  // event_loop();
  gtk_main();

  mPlayer->stop();

  mPlayer.reset();

  return 0;
}
