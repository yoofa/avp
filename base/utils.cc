/*
 * utils.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "utils.h"

#include <string>

#include <ctype.h>
#include <stdio.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace avp {

std::string nameForFd(int fd) {
  const size_t SIZE = 256;
  char buffer[SIZE];
  std::string result;
  snprintf(buffer, SIZE, "/proc/%d/fd/%d", getpid(), fd);
  struct stat s;
  if (lstat(buffer, &s) == 0) {
    if ((s.st_mode & S_IFMT) == S_IFLNK) {
      char linkto[256];
      int len = readlink(buffer, linkto, sizeof(linkto));
      if (len > 0) {
        if (len > 255) {
          linkto[252] = '.';
          linkto[253] = '.';
          linkto[254] = '.';
          linkto[255] = 0;
        } else {
          linkto[len] = 0;
        }
        result.append(linkto);
      }
    } else {
      result.append("unexpected type for ");
      result.append(buffer);
    }
  } else {
    result.append("couldn't open ");
    result.append(buffer);
  }
  return result;
}
} /* namespace avp */
