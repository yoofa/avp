/*
 * av_play.cc
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "av_play.h"

#include <fcntl.h>
#include <getopt.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <cstdio>
#include <iostream>
#include <memory>

#include <gtk/gtk.h>

#include "base/checks.h"
#include "base/logging.h"
// #include "examples/AudioFileRender.h"
// #include "examples/VideoFileRender.h"
#include "api/player.h"
#include "examples/avplay/gtk_window.h"

using namespace ave;
using ave::player::Player;

void printHelp() {
  std::cout << "help:\n"
               "  -f/--file <file>: file to play \n"
               "  -h/--helo\n";
  exit(1);
}

void event_loop() {}

class ExListener : public Player::Listener {
 public:
  ExListener() = default;
  ~ExListener() override = default;
  void OnCompletion() override { std::cout << "onCompletion" << std::endl; }

  void OnError(ave::status_t error) override {
    std::cout << "onError: " << error << std::endl;
  }
};

int main(int argc, char* argv[]) {
  gtk_init(&argc, &argv);
  std::string filename;
  std::string url;
  int choice = -1;
  while (1) {
    static struct option long_options[] = {{"help", no_argument, 0, 'h'},
                                           {"file", required_argument, 0, 'f'},
                                           {0, 0, 0, 0}};

    int option_index = 0;

    choice = getopt_long(argc, argv, "hf:", long_options, &option_index);

    if (choice == -1) {
      break;
    }

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
  ave::base::LogMessage::LogToDebug(ave::base::LogSeverity::LS_VERBOSE);

  std::unique_ptr<GtkWnd> gtkWindow(std::make_unique<GtkWnd>());
  gtkWindow->create();

  std::cout << "play file: " << filename << std::endl;
  url = std::string("file://") + filename;

  std::shared_ptr<ExListener> listener(std::make_shared<ExListener>());

  std::shared_ptr<Player> player = Player::Builder().build();
  AVE_DCHECK(player->SetListener(listener) == ave::OK);
  AVE_DCHECK(player->Init() == ave::OK);

  gtkWindow->addVideoRender();
  AVE_DCHECK(player->SetVideoRender(gtkWindow->videoRender()) == ave::OK);

  // Pass empty headers for local file playback per new API
  AVE_DCHECK(player->SetDataSource(url.c_str(), /*headers=*/{}) == ave::OK);

  AVE_DCHECK(player->Prepare() == ave::OK);

  // No explicit prepared callback in Player::Listener; start immediately
  player->Start();

  // event_loop();
  gtk_main();

  player->Stop();

  player.reset();

  return 0;
}
