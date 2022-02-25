/*
 * meta_data.cc
 * Copyright (C) 2022 youfa.song <vsyfar@gmail.com>
 *
 * Distributed under terms of the GPLv2 license.
 */

#include "meta_data.h"

#include <cstring>
#include <string>
#include <unordered_map>

#include "base/checks.h"
#include "base/logging.h"

namespace avp {

struct MetaData::typed_data {
  typed_data();
  ~typed_data();

  typed_data(const MetaData::typed_data&);
  typed_data& operator=(const MetaData::typed_data&);

  void clear();
  void setData(uint32_t type, const void* data, size_t size);
  void getData(uint32_t* type, const void** data, size_t* size) const;
  std::string asString(bool verbose) const;

 private:
  uint32_t mType;
  size_t mSize;

  union {
    void* ext_data;
    float reservoir;
  } u;

  bool usesReservoir() const { return mSize <= sizeof(u.reservoir); }

  void* allocateStorage(size_t size);
  void freeStorage();

  void* storage() { return usesReservoir() ? &u.reservoir : u.ext_data; }

  const void* storage() const {
    return usesReservoir() ? &u.reservoir : u.ext_data;
  }
};

struct MetaData::Rect {
  int32_t mLeft, mTop, mRight, mBottom;
};

struct MetaData::MetaDataInternal {
  std::unordered_map<uint32_t, MetaData::typed_data> mItems;
};

MetaData::MetaData() : mInternalData(new MetaDataInternal()) {}

MetaData::MetaData(const MetaData& rhs)
    : mInternalData(new MetaDataInternal()) {
  mInternalData->mItems = rhs.mInternalData->mItems;
}

MetaData& MetaData::operator=(const MetaData& rhs) {
  this->mInternalData->mItems = rhs.mInternalData->mItems;
  return *this;
}

MetaData::~MetaData() {
  clear();
  delete mInternalData;
}

void MetaData::clear() {
  mInternalData->mItems.clear();
}

bool MetaData::remove(uint32_t key) {
  auto search = mInternalData->mItems.find(key);
  if (search != mInternalData->mItems.end()) {
    mInternalData->mItems.erase(search);
    return true;
  } else {
    return false;
  }
}

bool MetaData::setCString(uint32_t key, const char* value) {
  return setData(key, TYPE_C_STRING, value, strlen(value) + 1);
}

bool MetaData::setInt32(uint32_t key, int32_t value) {
  return setData(key, TYPE_INT32, &value, sizeof(value));
}

bool MetaData::setInt64(uint32_t key, int64_t value) {
  return setData(key, TYPE_INT64, &value, sizeof(value));
}

bool MetaData::setFloat(uint32_t key, float value) {
  return setData(key, TYPE_FLOAT, &value, sizeof(value));
}

bool MetaData::setPointer(uint32_t key, void* value) {
  return setData(key, TYPE_POINTER, &value, sizeof(value));
}

bool MetaData::setRect(uint32_t key,
                       int32_t left,
                       int32_t top,
                       int32_t right,
                       int32_t bottom) {
  Rect r;
  r.mLeft = left;
  r.mTop = top;
  r.mRight = right;
  r.mBottom = bottom;

  return setData(key, TYPE_RECT, &r, sizeof(r));
}

/**
 * Note that the returned pointer becomes invalid when additional metadata is
 * set.
 */
bool MetaData::findCString(uint32_t key, const char** value) const {
  uint32_t type;
  const void* data;
  size_t size;
  if (!findData(key, &type, &data, &size) || type != TYPE_C_STRING) {
    return false;
  }

  *value = (const char*)data;

  return true;
}

bool MetaData::findInt32(uint32_t key, int32_t* value) const {
  uint32_t type = 0;
  const void* data;
  size_t size;
  if (!findData(key, &type, &data, &size) || type != TYPE_INT32) {
    return false;
  }

  CHECK_EQ(size, sizeof(*value));

  *value = *(int32_t*)data;

  return true;
}

bool MetaData::findInt64(uint32_t key, int64_t* value) const {
  uint32_t type = 0;
  const void* data;
  size_t size;
  if (!findData(key, &type, &data, &size) || type != TYPE_INT64) {
    return false;
  }

  CHECK_EQ(size, sizeof(*value));

  *value = *(int64_t*)data;

  return true;
}

bool MetaData::findFloat(uint32_t key, float* value) const {
  uint32_t type = 0;
  const void* data;
  size_t size;
  if (!findData(key, &type, &data, &size) || type != TYPE_FLOAT) {
    return false;
  }

  CHECK_EQ(size, sizeof(*value));

  *value = *(float*)data;

  return true;
}

bool MetaData::findPointer(uint32_t key, void** value) const {
  uint32_t type = 0;
  const void* data;
  size_t size;
  if (!findData(key, &type, &data, &size) || type != TYPE_POINTER) {
    return false;
  }

  CHECK_EQ(size, sizeof(*value));

  *value = *(void**)data;

  return true;
}

bool MetaData::findRect(uint32_t key,
                        int32_t* left,
                        int32_t* top,
                        int32_t* right,
                        int32_t* bottom) const {
  uint32_t type = 0;
  const void* data;
  size_t size;
  if (!findData(key, &type, &data, &size) || type != TYPE_RECT) {
    return false;
  }

  CHECK_EQ(size, sizeof(Rect));

  const Rect* r = (const Rect*)data;
  *left = r->mLeft;
  *top = r->mTop;
  *right = r->mRight;
  *bottom = r->mBottom;

  return true;
}

bool MetaData::setData(uint32_t key,
                       uint32_t type,
                       const void* data,
                       size_t size) {
  bool overwrote_existing = true;

  auto search = mInternalData->mItems.find(key);
  if (search == mInternalData->mItems.end()) {
    typed_data item;
    item.setData(type, data, size);
    mInternalData->mItems.insert(std::make_pair(key, item));
    overwrote_existing = false;
  } else {
    typed_data item = search->second;
    item.setData(type, data, size);
  }

  return overwrote_existing;
}

bool MetaData::findData(uint32_t key,
                        uint32_t* type,
                        const void** data,
                        size_t* size) const {
  auto search = mInternalData->mItems.find(key);
  if (search == mInternalData->mItems.end()) {
    return false;
  }

  const typed_data& item = search->second;
  item.getData(type, data, size);

  return true;
}

bool MetaData::hasData(uint32_t key) const {
  auto search = mInternalData->mItems.find(key);
  if (search == mInternalData->mItems.end()) {
    return false;
  }
  return true;
}

MetaData::typed_data::typed_data() : mType(0), mSize(0) {}

MetaData::typed_data::~typed_data() {
  clear();
}

MetaData::typed_data::typed_data(const typed_data& rhs)
    : mType(rhs.mType), mSize(0) {
  void* dst = allocateStorage(rhs.mSize);
  if (dst) {
    memcpy(dst, rhs.storage(), mSize);
  }
}

MetaData::typed_data& MetaData::typed_data::operator=(
    const MetaData::typed_data& rhs) {
  if (this != &rhs) {
    clear();
    mType = rhs.mType;
    void* dst = allocateStorage(rhs.mSize);
    if (dst) {
      memcpy(dst, rhs.storage(), mSize);
    }
  }

  return *this;
}

void MetaData::typed_data::clear() {
  freeStorage();

  mType = 0;
}

void MetaData::typed_data::setData(uint32_t type,
                                   const void* data,
                                   size_t size) {
  clear();

  mType = type;

  void* dst = allocateStorage(size);
  if (dst) {
    memcpy(dst, data, size);
  }
}

void MetaData::typed_data::getData(uint32_t* type,
                                   const void** data,
                                   size_t* size) const {
  *type = mType;
  *size = mSize;
  *data = storage();
}

void* MetaData::typed_data::allocateStorage(size_t size) {
  mSize = size;

  if (usesReservoir()) {
    return &u.reservoir;
  }

  u.ext_data = malloc(mSize);
  if (u.ext_data == NULL) {
    LOG(LS_ERROR) << "Couldn't allocate " << size << "bytes for item";
    mSize = 0;
  }
  return u.ext_data;
}

void MetaData::typed_data::freeStorage() {
  if (!usesReservoir()) {
    if (u.ext_data) {
      free(u.ext_data);
      u.ext_data = NULL;
    }
  }

  mSize = 0;
}

std::string MetaData::typed_data::asString(bool verbose) const {
  std::stringstream ss;
  const void* data = storage();
  switch (mType) {
    case TYPE_NONE:
      ss << "no type, size " << mSize;
      break;
    case TYPE_C_STRING:
      ss << "(char*) " << (const char*)data;
      break;
    case TYPE_INT32:
      ss << "(int32_t) " << *(int32_t*)data;
      break;
    case TYPE_INT64:
      ss << "(int64_t) " << *(int64_t*)data;
      break;
    case TYPE_FLOAT:
      ss << "(float) " << *(float*)data;
      break;
    case TYPE_POINTER:
      ss << "(void*) " << *(void**)data;
      break;
    case TYPE_RECT: {
      const Rect* r = (const Rect*)data;
      ss << "Rect(" << r->mLeft << ", " << r->mTop << ", " << r->mRight << ", "
         << r->mBottom << ") ";
      break;
    }

    default:
      ss << "(unknown type " << mType << ", size " << mSize << ")";
      if (verbose && mSize <= 48) {
        std::string foo;
        // hexdump(data, mSize, 0, &foo);
        ss << "\n";
        ss << foo.c_str();
      }
      break;
  }
  return ss.str();
}

static void MakeFourCCString(uint32_t x, char* s) {
  s[0] = x >> 24;
  s[1] = (x >> 16) & 0xff;
  s[2] = (x >> 8) & 0xff;
  s[3] = x & 0xff;
  s[4] = '\0';
}

std::string MetaData::toString() const {
  std::stringstream ss;
  ss << "<|";
  for (const auto& it : mInternalData->mItems) {
    int32_t key = it.first;
    char cc[5];
    MakeFourCCString(key, cc);
    const typed_data item = it.second;
    ss << " " << cc << ": " << item.asString(false) << " |";
  }
  ss << ">";
  return ss.str();
}

void MetaData::dumpToLog() const {
  for (const auto& it : mInternalData->mItems) {
    int32_t key = it.first;
    char cc[5];
    MakeFourCCString(key, cc);
    const typed_data item = it.second;
    LOG(LS_INFO) << cc << ": " << item.asString(true);
  }
}

} /* namespace avp */
