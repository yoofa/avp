/*
 *  Copyright (c) 2018 The WebRTC project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#ifndef AVP_BASE_SYSTEM_AVP_EXPORT_H_
#define AVP_BASE_SYSTEM_AVP_EXPORT_H_

// AVP_EXPORT is used to mark symbols as exported or imported when WebRTC is
// built or used as a shared library.
// When WebRTC is built as a static library the AVP_EXPORT macro expands to
// nothing.

#ifdef AVP_ENABLE_SYMBOL_EXPORT

#ifdef AVP_WIN

#ifdef AVP_LIBRARY_IMPL
#define AVP_EXPORT __declspec(dllexport)
#else
#define AVP_EXPORT __declspec(dllimport)
#endif

#else  // AVP_WIN

#if __has_attribute(visibility) && defined(AVP_LIBRARY_IMPL)
#define AVP_EXPORT __attribute__((visibility("default")))
#endif

#endif  // AVP_WIN

#endif  // AVP_ENABLE_SYMBOL_EXPORT

#ifndef AVP_EXPORT
#define AVP_EXPORT
#endif

#endif  // AVP_BASE_SYSTEM_AVP_EXPORT_H_
