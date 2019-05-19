/**
 * Copyright (c) 2011-2017 libbitcoin developers (see AUTHORS)
 *
 * This file is part of libbitcoin.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <bitcoin/bitcoin/unicode/unicode_streambuf.hpp>

#include <cstddef>
#include <cstring>
#include <iostream>
#include <streambuf>
#include <bitcoin/bitcoin/constants.hpp>
#include <bitcoin/bitcoin/unicode/unicode.hpp>
#include <bitcoin/bitcoin/utility/assert.hpp>

namespace libbitcoin {

// Local definition for max number of bytes in a utf8 character.
constexpr size_t utf8_max_character_size = 4;

unicode_streambuf::unicode_streambuf(std::wstreambuf* wide_buffer, size_t size)
  : wide_size_(size), narrow_size_(wide_size_ * utf8_max_character_size),
    narrow_(new char[narrow_size_]), wide_(new wchar_t[narrow_size_]),
    wide_buffer_(wide_buffer)
{
    if (wide_size_ > (bc::max_uint64 / utf8_max_character_size))
        throw std::ios_base::failure(
            "Wide buffer must be no more than one fourth of max uint64.");

    // Input buffer is not yet populated, reflect zero length buffer here.
    setg(narrow_, narrow_, narrow_);

    // Output buffer is underexposed by 1 byte to accomodate the overflow byte.
    setp(narrow_, &narrow_[narrow_size_ - 1]);
}

unicode_streambuf::~unicode_streambuf()
{
    sync();
    delete[] wide_;
    delete[] narrow_;
}

// Read characters from input buffer.
// This invokes wide_buffer_.xsgetn() which requires a patch for
// console (keyboard) input on Windows, so ensure this class is
// initialized with a patched std::wcin when std::wcin is used.
std::streambuf::int_type unicode_streambuf::underflow()
{
    // streamsize is signed.
    BITCOIN_ASSERT(wide_size_ > 0 && wide_size_ <= bc::max_int64);
    const auto size = static_cast<std::streamsize>(wide_size_);

    // Read from the wide input buffer.
    const auto read = static_cast<size_t>(wide_buffer_->sgetn(wide_, size));

    // Handle read termination.
    if (read == 0)
        return traits_type::eof();

    // Convert utf16 to utf8, returning bytes written.
    const auto bytes = to_utf8(narrow_, narrow_size_, wide_, read);

    // Reset gptr and egptr, eback never changes.
    setg(narrow_, narrow_, &narrow_[bytes]);

    // Return the first character in the input sequence.
    return traits_type::to_int_type(*gptr());
}

// Write characters to output buffer.
// We compensate for character-splitting. This is necessary because
// MSVC does not support a UTF8 locale and as such streams interpret
// narrow characters in the default locale. This implementation
// assumes the stream will treat each byte of a multibyte narrow
// chracter as an individual single byte character.
std::streambuf::int_type unicode_streambuf::overflow(
    std::streambuf::int_type character)
{
    // Add a single explicitly read byte to the buffer.
    // The narrow buffer is underexposed by 1 byte to accomodate this.
    if (character != traits_type::eof())
    {
        *pptr() = static_cast<char>(character);
        pbump(sizeof(char));
    }

    // This will be in the range 0..4, indicating the number of bytes that were
    // not written in the conversion. A nonzero value results when the buffer
    // terminates within a utf8 multiple byte character.
    uint8_t unwritten = 0;

    // Get the number of bytes in the buffer to convert.
    const auto write = pptr() - pbase();

    if (write > 0)
    {
        // Convert utf8 to utf16, returning chars written and bytes unread.
        const auto chars = to_utf16(wide_, narrow_size_, narrow_, write,
            unwritten);

        // Write to the wide output buffer.
        const auto written = wide_buffer_->sputn(wide_, chars);

        // Handle write failure as an EOF.
        if (written != chars)
            return traits_type::eof();
    }

    // Copy the fractional character to the beginning of the buffer.
    memcpy(narrow_, &narrow_[write - unwritten], unwritten);

    // Reset the pptr to the buffer start, leave pbase and epptr.
    // We could use just pbump for this if it wasn't limited to 'int' width.
    setp(narrow_, &narrow_[narrow_size_ - 1]);

    // Reset pptr just after the fractional character.
    pbump(unwritten);

    // Return the overflow byte or EOF sentinel.
    return character;
};

// Flush our output sequence.
int unicode_streambuf::sync()
{
    const int success = 0;
    const int failure = -1;

    // We expect EOF to be returned, because we passed it.
    if (overflow(traits_type::eof()) == traits_type::eof())
        return success;

    return failure;
}

} // namespace libbitcoin
