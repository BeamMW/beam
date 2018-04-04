#include "logger_checkpoints.h"

namespace beam {

static thread_local Checkpoint* currentCheckpoint;
static thread_local Checkpoint* rootCheckpoint;

Checkpoint::Checkpoint(detail::CheckpointItem* items, size_t maxItems, const char* file, int line, const char* function) :
    _from(file, line, function), _items(items), _ptr(items), _maxItems(maxItems)
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
    
void Checkpoint::flush() {
    if (_maxItems != 0 && _ptr > _items) {
        LogMessage m = LogMessage::create(3, _from);
        m << " CHECKPOINT:\n";
        auto* p = _items;
        while (p < _ptr) {
            p->fn(m, &p->data);
            ++p;
        }
        _maxItems = 0; //i.e. flushed
    }
    if (_next) {
        _next->flush();
    }
}
    
Checkpoint::~Checkpoint() {
    if (_maxItems != 0 && std::uncaught_exception()) {
        flush();
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
