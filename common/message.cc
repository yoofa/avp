/*
 * Message.cc
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "message.h"

#include <memory>

#include "base/errors.h"
#include "handler.h"
#include "looper.h"

namespace avp {

status_t ReplyToken::setReply(const std::shared_ptr<Message>& reply) {
  if (mReplied) {
    return -1;
  }
  std::cout << "setReply" << std::endl;
  // CHECK(mReply == nullptr);
  mReply = reply;
  mReplied = true;
  return 0;
}

Message::Message() : mWhat(0), mHandlerId(0) {}

Message::Message(uint32_t what, const std::shared_ptr<Handler> handler)
    : mWhat(what) {
  setHandler(handler);
}

Message::~Message() {
  clear();
}

void Message::setWhat(uint32_t what) {
  mWhat = what;
}

uint32_t Message::what() const {
  return mWhat;
}

void Message::setHandler(const std::shared_ptr<Handler> handler) {
  if (handler == nullptr || handler.get() == nullptr) {
    mHandlerId = 0;
    mHandler.reset();
    mLooper.reset();

  } else {
    mHandlerId = handler->id();
    mHandler = handler;
    mLooper = handler->getLooper();
  }
}

status_t Message::post(int64_t delayUs) {
  auto looper = mLooper.lock();
  if (looper.get() != nullptr) {
    looper->post(shared_from_this(), delayUs);
  }
  return 0;
}

status_t Message::postAndWaitResponse(std::shared_ptr<Message>& response) {
  std::shared_ptr<Looper> looper = mLooper.lock();
  if (looper == nullptr) {
    return -1;
  }

  std::shared_ptr<ReplyToken> replyToken = looper->createReplyToken();
  if (replyToken == nullptr) {
    return -1;
  }

  setReplyToken("replyID", replyToken);
  looper->post(shared_from_this(), 0);
  return looper->awaitResponse(replyToken, response);
}

bool Message::senderAwaitsResponse(std::shared_ptr<ReplyToken>& replyId) {
  bool found = findReplyToken("replyID", replyId);
  if (!found) {
    return false;
  }
  return replyId.get() != nullptr;
}

status_t Message::postReply(const std::shared_ptr<ReplyToken>& replyId) {
  if (replyId == nullptr) {
    return -1;
  }

  std::shared_ptr<Looper> looper = replyId->getLooper();
  if (looper == nullptr) {
    return -1;
  }

  return looper->postReply(replyId, shared_from_this());
}

void Message::clear() {
  mItems.clear();
}

// void Message::setObject(const char* name, std::shared_ptr<MessageObject>&
// obj) {
//  mObject = obj;
//}
//
// bool Message::getObject(const char* name,
//                        std::shared_ptr<MessageObject>& obj) const {
//  if (mObject.get() != nullptr) {
//    obj = std::move(mObject);
//    return true;
//  }
//  return false;
//}
//

std::shared_ptr<Message::Item> Message::allocateItem(const char* name) {
  auto search = mItems.find(name);
  if (search != mItems.end()) {
    mItems.erase(search);
  }
  auto result = mItems.emplace(name, std::make_shared<Message::Item>());
  return result.first->second;
}

std::shared_ptr<Message::Item> Message::findItem(const char* name,
                                                 Type type) const {
  auto search = mItems.find(name);
  if (search != mItems.end()) {
    auto item = search->second;
    if (item->mType == type) {
      return item;
    }
  }
  return nullptr;
}

bool Message::contains(const char* name) const {
  auto search = mItems.find(name);
  return search != mItems.end();
}

#define BASIC_TYPE(NAME, TYPENAME)                                    \
  void Message::set##NAME(const char* name, TYPENAME value) {         \
    std::shared_ptr<Message::Item> item = allocateItem(name);         \
    item->mType = Message::kType##NAME;                               \
    item->value = value;                                              \
  }                                                                   \
                                                                      \
  bool Message::find##NAME(const char* name, TYPENAME* value) const { \
    std::shared_ptr<avp::Message::Item> item;                         \
    item = findItem(name, Message::kType##NAME);                      \
    if (item) {                                                       \
      *value = std::get<TYPENAME>(item->value);                       \
      return true;                                                    \
    }                                                                 \
    return false;                                                     \
  }

BASIC_TYPE(Int32, int32_t)
BASIC_TYPE(Int64, int64_t)
BASIC_TYPE(Size, size_t)
BASIC_TYPE(Float, float)
BASIC_TYPE(Double, double)
BASIC_TYPE(Pointer, void*)

#undef BASIC_TYPE

void Message::setRect(const char* name,
                      int32_t left,
                      int32_t top,
                      int32_t right,
                      int32_t bottom) {
  std::shared_ptr<Message::Item> item = allocateItem(name);
  item->mType = Message::kTypeRect;
  item->value = Rect{left, top, right, bottom};
}

bool Message::findRect(const char* name,
                       int32_t* left,
                       int32_t* top,
                       int32_t* right,
                       int32_t* bottom) const {
  std::shared_ptr<Message::Item> item;
  item = findItem(name, Message::kTypeRect);
  if (item) {
    Rect rect = std::get<Rect>(item->value);
    *left = rect.mLeft;
    *top = rect.mTop;
    *right = rect.mRight;
    *bottom = rect.mBottom;
    return true;
  }
  return false;
}

void Message::setString(const char* name, const std::string& s) {
  std::shared_ptr<Message::Item> item = allocateItem(name);
  item->mType = Message::kTypeString;
  item->value = s;
}

void Message::setString(const char* name, const char* s, ssize_t len) {
  std::shared_ptr<Message::Item> item = allocateItem(name);
  item->mType = Message::kTypeString;
  item->value = std::string(s, len > 0 ? len : strlen(s));
}

bool Message::findString(const char* name, std::string& value) const {
  std::shared_ptr<Message::Item> item;
  item = findItem(name, kTypeString);
  if (item) {
    value = std::get<std::string>(item->value);
    return true;
  }

  return false;
}

void Message::setMessage(const char* name, const std::shared_ptr<Message> msg) {
  std::shared_ptr<Message::Item> item = allocateItem(name);
  item->mType = Message::kTypeMessage;
  item->value = std::move(msg);
}

bool Message::findMessage(const char* name,
                          std::shared_ptr<Message>& msg) const {
  std::shared_ptr<Message::Item> item;
  item = findItem(name, kTypeMessage);
  if (item) {
    msg = std::get<std::shared_ptr<Message>>(item->value);
    return true;
  }

  return false;
}

void Message::setReplyToken(const char* name,
                            const std::shared_ptr<ReplyToken> token) {
  std::shared_ptr<Message::Item> item = allocateItem(name);
  item->mType = kTypeToken;
  item->value = std::move(token);
}

bool Message::findReplyToken(const char* name,
                             std::shared_ptr<ReplyToken>& token) const {
  std::shared_ptr<Message::Item> item;
  item = findItem(name, kTypeToken);
  if (item) {
    auto result = std::get<std::shared_ptr<ReplyToken>>(item->value);
    token = std::move(result);
    return true;
  }
  return false;
}

void Message::setBuffer(const char* name,
                        const std::shared_ptr<avp::Buffer> buffer) {
  std::shared_ptr<Message::Item> item = allocateItem(name);
  item->mType = kTypeBuffer;
  item->value = std::move(buffer);
}

bool Message::findBuffer(const char* name,
                         std::shared_ptr<avp::Buffer>& buffer) const {
  std::shared_ptr<Message::Item> item;
  item = findItem(name, kTypeBuffer);
  if (item) {
    auto result = std::get<std::shared_ptr<avp::Buffer>>(item->value);
    buffer = std::move(result);
    return true;
  }
  return false;
}

void Message::setObject(const char* name,
                        const std::shared_ptr<avp::MessageObject> obj) {
  std::shared_ptr<Message::Item> item = allocateItem(name);
  item->mType = kTypeObject;
  item->value = std::move(obj);
}

bool Message::findObject(const char* name,
                         std::shared_ptr<avp::MessageObject>& obj) const {
  std::shared_ptr<Message::Item> item;
  item = findItem(name, kTypeObject);
  if (item) {
    auto result = std::get<std::shared_ptr<avp::MessageObject>>(item->value);
    obj = std::move(result);
    return true;
  }
  return false;
}

std::shared_ptr<Message> Message::dup() const {
  std::shared_ptr<Message> message =
      std::make_shared<Message>(mWhat, mHandler.lock());

  return message;
}

void Message::deliver() {
  auto handler = mHandler.lock();
  if (handler.get() != nullptr) {
    handler->deliverMessage(shared_from_this());
  }
}

}  // namespace avp
