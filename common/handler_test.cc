/*
 * HandlerTest.cc
 * Copyright (C) 2021 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include <chrono>
#include <iostream>
#include <memory>
#include <thread>

#include "handler.h"
#include "looper.h"
#include "message.h"

namespace avp {

class TestHandler : public avp::Handler {
 public:
  TestHandler() = default;
  virtual ~TestHandler() = default;

  void onMessageReceived(const std::shared_ptr<avp::Message>& message) override;

 private:
};

void avp::TestHandler::onMessageReceived(
    const std::shared_ptr<avp::Message>& message) {
  uint32_t what = message->what();
  std::cout << "Testhandler recv what:" << what << std::endl;
  switch (what) {
    case 11:
      int numInt32;
      float numFloat;
      int32_t left, top, right, bottom;
      message->findInt32("Int32", &numInt32);
      message->findFloat("Float", &numFloat);
      message->findRect("Rect", &left, &top, &right, &bottom);
      std::cout << "case 11, Int32:" << numInt32 << ", Float:" << numFloat
                << ", Rect:{" << left << "," << top << "," << right << ","
                << bottom << "}" << std::endl;
      break;
    case 12: {
      std::string mString;
      message->findString("String", mString);
      std::shared_ptr<Message> msg;
      message->findMessage("Message", msg);

      std::cout << "case 12, String:" << mString
                << ", Message.what:" << msg->what() << std::endl;
      break;
    }
    case 13: {
      std::cout << "case 13 start" << std::endl;
      std::shared_ptr<avp::ReplyToken> reply;
      message->senderAwaitsResponse(reply);
      std::shared_ptr<Message> response = std::make_shared<Message>();
      response->setInt32("err", -2);
      response->postReply(reply);
      break;
    }

    default:
      break;
  }
}

}  // namespace avp

int main() {
  using namespace std::chrono_literals;
  {
    std::shared_ptr<avp::Looper> mLooper(std::make_shared<avp::Looper>());

    std::shared_ptr<avp::TestHandler> mHandler(
        std::make_shared<avp::TestHandler>());

    mLooper->setName("TestLooper");
    mLooper->start();

    avp::Looper::handler_id id = mLooper->registerHandler(mHandler);

    std::shared_ptr<avp::Message> message(std::make_shared<avp::Message>());
    message->setHandler(mHandler);

    std::cout << "post 11" << std::endl;
    message->setWhat(11);
    message->setInt32("Int32", 16);
    message->setFloat("Float", 3.14);
    message->setRect("Rect", 1, 2, 3, 4);
    message->post(1.5 * 1000 * 1000);

    std::this_thread::sleep_for(1000ms);

    {
      std::cout << "post 12" << std::endl;
      auto msg2 = message->dup();
      msg2->setWhat(12);
      std::string str = "hello";
      msg2->setString("String", str);
      str = "world";
      auto msg3 = message->dup();
      msg3->setWhat(100);

      msg2->setMessage("Message", msg3);
      msg2->post(2 * 1000 * 1000);
    }

    std::cout << "post 13" << std::endl;
    auto msg4 = message->dup();
    msg4->setWhat(13);
    std::shared_ptr<avp::Message> response;
    msg4->postAndWaitResponse(response);

    int err;
    bool found = response->findInt32("err", &err);

    if (found) {
      std::cout << "after 13, get message.err: " << err << std::endl;
    } else {
      std::cout << "after 13, not get message.err" << std::endl;
    }

    std::this_thread::sleep_for(3000ms);
    mLooper->unregisterHandler(id);

    std::cout << "goto stop" << std::endl;

    mLooper->stop();
    std::cout << "after stop" << std::endl;
    std::this_thread::sleep_for(3000ms);
  }
  std::cout << "out of scope" << std::endl;
  std::this_thread::sleep_for(3000ms);
}
