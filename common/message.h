/*
 * Message.h
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#ifndef AVP_MESSAGE_H
#define AVP_MESSAGE_H

#include <iostream>
#include <memory>
#include <optional>
#include <unordered_map>
#include <variant>

#include "base/constructor_magic.h"
#include "base/errors.h"
#include "looper.h"

namespace avp {

class Handler;
class Buffer;

class MessageObject {
 public:
  MessageObject() = default;
  virtual ~MessageObject() = default;
};

class ReplyToken : public MessageObject {
 public:
  explicit ReplyToken(const std::shared_ptr<Looper>& looper)
      : mLooper(looper), mReplied(false) {}
  virtual ~ReplyToken() = default;

 private:
  friend class Message;
  friend class Looper;
  std::weak_ptr<Looper> mLooper;
  std::shared_ptr<Message> mReply;
  bool mReplied;

  std::shared_ptr<Looper> getLooper() const { return mLooper.lock(); }

  status_t setReply(const std::shared_ptr<Message>& reply);

  bool getReply(std::shared_ptr<Message>& reply) {
    if (mReplied) {
      reply = std::move(mReply);
    }

    return mReplied;
  }
};

class Message : public std::enable_shared_from_this<Message> {
 public:
  enum Type {
    kTypeInt32,
    kTypeInt64,
    kTypeSize,
    kTypeFloat,
    kTypeDouble,
    kTypePointer,
    kTypeRect,
    kTypeString,
    kTypeMessage,
    kTypeToken,
    kTypeBuffer,
    kTypeObject,
  };

  struct Rect {
    int32_t mLeft, mTop, mRight, mBottom;
  };

  struct Item {
    std::variant<int32_t,
                 int64_t,
                 size_t,
                 float,
                 double,
                 void*,
                 Rect,
                 std::string,
                 std::shared_ptr<Message>,
                 std::shared_ptr<ReplyToken>,
                 std::shared_ptr<Buffer>,
                 std::shared_ptr<MessageObject>>
        value;
    Type mType;
  };

  Message();
  explicit Message(uint32_t what, const std::shared_ptr<Handler> handler);
  virtual ~Message();

  void setWhat(uint32_t what);
  uint32_t what() const;
  void setHandler(const std::shared_ptr<Handler> handler);

  void clear();

  void setInt32(const char* name, int32_t value);
  void setInt64(const char* name, int64_t value);
  void setSize(const char* name, size_t value);
  void setFloat(const char* name, float value);
  void setDouble(const char* name, double value);
  void setPointer(const char* name, void* value);
  void setString(const char* name, const char* s, ssize_t len = -1);
  void setString(const char* name, const std::string& s);
  void setMessage(const char* name, const std::shared_ptr<Message> msg);
  void setReplyToken(const char* name, const std::shared_ptr<ReplyToken> token);
  void setBuffer(const char* name, const std::shared_ptr<Buffer> buffer);
  void setObject(const char* name, const std::shared_ptr<MessageObject> obj);
  void setRect(const char* name,
               int32_t left,
               int32_t top,
               int32_t right,
               int32_t bottom);

  bool contains(const char* name) const;

  bool findInt32(const char* name, int32_t* value) const;
  bool findInt64(const char* name, int64_t* value) const;
  bool findSize(const char* name, size_t* value) const;
  bool findFloat(const char* name, float* value) const;
  bool findDouble(const char* name, double* value) const;
  bool findPointer(const char* name, void** value) const;
  bool findString(const char* name, std::string& value) const;
  bool findMessage(const char* name, std::shared_ptr<Message>& msg) const;
  bool findReplyToken(const char* name,
                      std::shared_ptr<ReplyToken>& token) const;
  bool findBuffer(const char* name, std::shared_ptr<Buffer>& buffer) const;
  bool findObject(const char* name, std::shared_ptr<MessageObject>& obj) const;
  bool findRect(const char* name,
                int32_t* left,
                int32_t* top,
                int32_t* right,
                int32_t* bottom) const;

  status_t post(int64_t delayUs = 0LL);

  status_t postAndWaitResponse(std::shared_ptr<Message>& response);

  bool senderAwaitsResponse(std::shared_ptr<ReplyToken>& replyId);

  status_t postReply(const std::shared_ptr<ReplyToken>& replyId);

  // return a depp copy of Message
  std::shared_ptr<Message> dup() const;

 private:
  friend class Looper;  // for deliver()

  uint32_t mWhat;
  Looper::handler_id mHandlerId;
  std::weak_ptr<Handler> mHandler;
  std::weak_ptr<Looper> mLooper;
  std::unordered_map<std::string, std::shared_ptr<Item>> mItems;

  std::shared_ptr<Item> allocateItem(const char* name);
  std::shared_ptr<Item> findItem(const char* name, Type) const;
  void deliver();

  AVP_DISALLOW_COPY_AND_ASSIGN(Message);
};

}  // namespace avp

#endif /* !AVP_MESSAGE_H */
