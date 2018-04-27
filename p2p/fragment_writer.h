#pragma once
#include "utility/io/buffer.h"
#include <functional>

namespace beam {

/// For long messages, it collects raw bytes of unknown size (typically from serializer)
/// and arranges them into fixed-length shared fragments to be forwarded to network.
/// For small messages, it reuses shared regions and minimizes allocation calls
class FragmentWriter {
public:
    /// Called when either current fragment is filled or current message is finalized
    using OnNewFragment = std::function<void(io::SharedBuffer&& fragment)>;

    /// Ctor
    FragmentWriter(size_t fragmentSize, size_t headerSize, const OnNewFragment& callback);

    /// Writes new data into fragments. Invokes callback if current fragment gets full
    void* write(const void *ptr, size_t size);

    /// Finalizes current message: invokes callback
    void finalize();

private:
    /// Invokes callback if there's any data written
    void call();

    /// Creates a new fragment
    void new_fragment();

    /// Fixed fragment size in bytes
    const size_t _fragmentSize;

    /// Size of header that cannot be splitted between fragments
    const size_t _headerSize;

    /// Callback
    OnNewFragment _callback;

    /// Shared ptr (memory guard), current fragment
    io::SharedMem _fragment;

    /// Beginning of the current message
    char* _msgBase=0;

    /// Write cursor inside the fragment
    char* _cursor=0;

    /// Bytes remaining in the current fragment
    size_t _remaining=0;
};

} //namespace
