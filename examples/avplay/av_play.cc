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
#include <string>

#include "base/checks.h"
#include "base/logging.h"
#include "api/player.h"
#include "examples/avplay/file_sink.h"

using namespace ave;
using ave::player::Player;
using ave::player::YuvFileVideoRender;
using ave::player::FileAudioDevice;

void printHelp() {
  std::cout << "help:\n"
               "  -f/--file <file>   : file to play\n"
               "  -o/--output <base> : write output to <base>.yuv and <base>.wav\n"
               "                       (headless mode, no display needed)\n"
               "  -d/--duration <s>  : stop after <s> seconds (default: full)\n"
               "  -h/--help\n";
  exit(1);
}

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
  std::string filename;
  std::string output_base;
  int duration_s = 0;
  int choice = -1;

  while (1) {
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {"file", required_argument, 0, 'f'},
        {"output", required_argument, 0, 'o'},
        {"duration", required_argument, 0, 'd'},
        {0, 0, 0, 0}};

    int option_index = 0;
    choice = getopt_long(argc, argv, "hf:o:d:", long_options, &option_index);
    if (choice == -1) {
      break;
    }
    switch (choice) {
      case 'f':
        filename = std::string(optarg);
        break;
      case 'o':
        output_base = std::string(optarg);
        break;
      case 'd':
        duration_s = std::atoi(optarg);
        break;
      case 'h':
      case '?':
      default:
        printHelp();
        break;
    }
  }

  if (filename.empty()) {
    printHelp();
  }

  ave::base::LogMessage::LogToDebug(ave::base::LogSeverity::LS_INFO);

  std::cout << "play file: " << filename << std::endl;
  std::string url = std::string("file://") + filename;

  std::shared_ptr<ExListener> listener(std::make_shared<ExListener>());

  bool headless = !output_base.empty();

  if (headless) {
    // ── Headless file-output mode ────────────────────────────────────────────
    std::string yuv_path = output_base + ".yuv";
    std::string wav_path = output_base + ".wav";
    std::cout << "Headless mode: writing video to " << yuv_path
              << ", audio to " << wav_path << std::endl;

    auto video_render = std::make_shared<YuvFileVideoRender>(yuv_path);
    auto audio_device = std::make_shared<FileAudioDevice>(wav_path);

    std::shared_ptr<Player> player =
        Player::Builder()
            .setAudioDeviceFactory(audio_device)
            .build();

    AVE_DCHECK(player->SetListener(listener) == ave::OK);
    AVE_DCHECK(player->Init() == ave::OK);
    // File output mode: disable A/V sync so all frames are rendered immediately
    // without clock-based pacing (audio writes too fast in file mode).
    player->SetSyncEnabled(false);
    AVE_DCHECK(player->SetVideoRender(video_render) == ave::OK);
    AVE_DCHECK(player->SetDataSource(url.c_str(), {}) == ave::OK);
    AVE_DCHECK(player->Prepare() == ave::OK);
    sleep(2);
    player->Start();

    int wait_s = (duration_s > 0) ? duration_s : 40;
    std::cout << "Playing for up to " << wait_s << "s ..." << std::endl;
    sleep(wait_s);

    player->Stop();
    player.reset();

    std::cout << "Video frames written: " << video_render->frames_written()
              << " (" << video_render->width() << "x" << video_render->height()
              << ")" << std::endl;

  } else {
    // ── GTK display mode ─────────────────────────────────────────────────────
#if defined(AVE_LINUX)
    // Late-include GTK only in display mode to avoid compile dependency issues
    // in headless mode. We rely on the gtk_window library being linked.
    extern void gtk_main_avplay(const std::string& url,
                                std::shared_ptr<Player::Listener> listener);
    gtk_main_avplay(url, listener);
#else
    std::cerr << "Display mode not supported. Use -o for headless output."
              << std::endl;
    return 1;
#endif
  }

  return 0;
}

