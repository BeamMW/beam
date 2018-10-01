// Copyright 2018 The Beam Team
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "logger_checkpoints.h"

namespace beam {

static thread_local Checkpoint* currentCheckpoint;
static thread_local Checkpoint* rootCheckpoint;

Checkpoint* current_checkpoint() {
    return currentCheckpoint;
}

void flush_all_checkpoints(LogMessage* to) {
    if (rootCheckpoint) rootCheckpoint->flush(to);
}

void flush_last_checkpoint(LogMessage* to) {
    if (currentCheckpoint) currentCheckpoint->flush(to);
}

Checkpoint::Checkpoint(detail::CheckpointItem* items, size_t maxItems, const char* file, int line, const char* function) :
    _header(LOG_LEVEL_ERROR, file, line, function), _items(items), _ptr(items), _maxItems(maxItems)
{
    assert(maxItems > 0);
    if (currentCheckpoint == 0) {
        assert(rootCheckpoint == 0);
        currentCheckpoint = this;
        rootCheckpoint = this;
        _prev = 0;
    } else {
        currentCheckpoint->_next = this;
        _prev = currentCheckpoint;
        currentCheckpoint = this;
    }
    _next = 0;
}

void Checkpoint::flush_to(LogMessage* to) {
    assert(to);
    *to << "\n";
    auto* p = _items;
    while (p < _ptr) {
        p->fn(*to, &p->data);
        ++p;
    }
    _maxItems = 0; //i.e. flushed
    if (_next) {
        _next->flush(to);
    }
}

void Checkpoint::flush(LogMessage* to) {
    if (_maxItems == 0) return;
    if (_ptr > _items) {
        if (!to)
            flush_from_here();
        else {
            flush_to(to);
        }
    }
}

void Checkpoint::flush_from_here() {
    LogMessage m(_header);
    flush_to(&m);
}

Checkpoint::~Checkpoint() {
    if (_maxItems != 0 && std::uncaught_exceptions()) {
        flush_all_checkpoints(0);
    }
    assert(currentCheckpoint == this);
    if (_prev) {
        _prev->_next = 0;
    }
    currentCheckpoint = _prev;
    if (currentCheckpoint == 0) {
        rootCheckpoint = 0;
    }
}

} //namespace
