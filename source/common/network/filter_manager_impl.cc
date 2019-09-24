#include "common/network/filter_manager_impl.h"

#include <list>

#include "envoy/network/connection.h"

#include "common/common/assert.h"

#include <iostream>

namespace Envoy {
namespace Network {

void FilterManagerImpl::addWriteFilter(WriteFilterSharedPtr filter) {
  ASSERT(connection_.state() == Connection::State::Open);
  ActiveWriteFilterPtr new_filter(new ActiveWriteFilter{*this, filter});
  filter->initializeWriteFilterCallbacks(*new_filter);
  new_filter->moveIntoList(std::move(new_filter), downstream_filters_);
}

void FilterManagerImpl::addFilter(FilterSharedPtr filter) {
  addReadFilter(filter);
  addWriteFilter(filter);
}

void FilterManagerImpl::addReadFilter(ReadFilterSharedPtr filter) {
  ASSERT(connection_.state() == Connection::State::Open);
  ActiveReadFilterPtr new_filter(new ActiveReadFilter{*this, filter});
  filter->initializeReadFilterCallbacks(*new_filter);
  new_filter->moveIntoListBack(std::move(new_filter), upstream_filters_);
}

bool FilterManagerImpl::initializeReadFilters() {
  if (upstream_filters_.empty()) {
    return false;
  }
  onContinueReading(nullptr, connection_);
  return true;
}

void FilterManagerImpl::onContinueReading(ActiveReadFilter* filter,
                                          ReadBufferSource& buffer_source) {
  // Filter could return status == FilterStatus::StopIteration immediately, close the connection and
  // use callback to call this function.
  if (connection_.state() != Connection::State::Open) {
    return;
  }

  std::list<ActiveReadFilterPtr>::iterator entry;
  if (!filter) {
    std::cout << "filter parameter not empty" << std::endl;
    entry = upstream_filters_.begin();
  } else {
    std::cout << "filter parameter is empty" << std::endl;
    entry = std::next(filter->entry());
  }

  int counter =  0;
  for (; entry != upstream_filters_.end(); entry++) {
    counter++;
    if (!(*entry)->initialized_) {
      (*entry)->initialized_ = true;
      FilterStatus status = (*entry)->filter_->onNewConnection();
      if (status == FilterStatus::StopIteration || connection_.state() != Connection::State::Open) {
        std::cout << "early return at point1" << std::endl;
        return;
      }
    }

    StreamBuffer read_buffer = buffer_source.getReadBuffer();
    std::cout << "before onData call, buffer length: " << read_buffer.buffer.length() << std::endl;
    if (read_buffer.buffer.length() > 0 || read_buffer.end_stream) {
      FilterStatus status = (*entry)->filter_->onData(read_buffer.buffer, read_buffer.end_stream);
      if (status == FilterStatus::StopIteration || connection_.state() != Connection::State::Open) {
        std::cout << "early return at point2" << std::endl;
        return;
      }
    }
  }
  std::cout << "count number of filters: " << counter << std::endl;
}

void FilterManagerImpl::onRead() {
  ASSERT(!upstream_filters_.empty());
  onContinueReading(nullptr, connection_);
}

FilterStatus FilterManagerImpl::onWrite() { return onWrite(nullptr, connection_); }

FilterStatus FilterManagerImpl::onWrite(ActiveWriteFilter* filter,
                                        WriteBufferSource& buffer_source) {
  // Filter could return status == FilterStatus::StopIteration immediately, close the connection and
  // use callback to call this function.
  if (connection_.state() != Connection::State::Open) {
    return FilterStatus::StopIteration;
  }

  std::list<ActiveWriteFilterPtr>::iterator entry;
  if (!filter) {
    entry = downstream_filters_.begin();
  } else {
    entry = std::next(filter->entry());
  }

  for (; entry != downstream_filters_.end(); entry++) {
    StreamBuffer write_buffer = buffer_source.getWriteBuffer();
    FilterStatus status = (*entry)->filter_->onWrite(write_buffer.buffer, write_buffer.end_stream);
    if (status == FilterStatus::StopIteration || connection_.state() != Connection::State::Open) {
      return FilterStatus::StopIteration;
    }
  }

  return FilterStatus::Continue;
}

void FilterManagerImpl::onResumeWriting(ActiveWriteFilter* filter,
                                        WriteBufferSource& buffer_source) {
  auto status = onWrite(filter, buffer_source);
  if (status == FilterStatus::Continue) {
    StreamBuffer write_buffer = buffer_source.getWriteBuffer();
    connection_.rawWrite(write_buffer.buffer, write_buffer.end_stream);
  }
}

} // namespace Network
} // namespace Envoy
