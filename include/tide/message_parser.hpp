#ifndef TIDE_MESSAGE_PARSER_HEADER
#define TIDE_MESSAGE_PARSER_HEADER

#include "view.hpp"

#include <cstdint>
#include <vector>

namespace tide {

struct message
{
    // A keep alive message has no identifier (it's just 4 zero bytes), this is used
    // only by `message_parser::type` to tell caller that a message is a `keep_alive`.
    static constexpr int keep_alive = -1;

    /**
     * All currently supported BitTorrent message types. This is denoted by the 5th byte
     * in a torrent message (the byte values are shown next to each type).
     *
     * NOTE: whereas in the protocol the term 'piece' is used to describe both the
     * pieces into which a file is split and the payload sent over the network, for
     * clarity, here and throughout the application, the unofficial term 'block' is used
     * to denote the piece chunks sent over the network, while 'piece' is only used to
     * describe the file partitions.
     */
    enum type
    {
        // -- standard BitTorrent messages --
        choke = 0,
        unchoke = 1,
        interested = 2,
        not_interested = 3,
        have = 4,
        bitfield = 5,
        request = 6,
        block = 7,
        cancel = 8,
        // -- DHT extension messages --
        port = 9,
        // -- Fast extension messages --
        suggest_piece = 13,
        have_all = 14,
        have_none = 15,
        reject_request = 16,
        allowed_fast = 17,
    };

    int type;
    // The message content, excluding the four bytes of message length and one byte of
    // message identifier/type.
    const_view<uint8_t> data;
};

struct handshake
{
    // Variable length string identifying the protocol. Currently "Bittorrent protocol".
    const_view<uint8_t> protocol;
    // 8 reserved bytes used to identify extensions.
    const_view<uint8_t> reserved;
    // This is the torrent's (20 byte) SHA-1 hash to which the peer belongs.
    const_view<uint8_t> info_hash;
    // The peer's client (software) can usually be extracted from here. It is also a 20
    // byte string.
    const_view<uint8_t> peer_id;
};

/**
 * Raw bytes sent by peer are read into message_parser's internal buffer. These are
 * lazily parsed into BitTorrent messages, however, no extra copies are made. The user
 * receives merely a view into the internal buffer.
 *
 * The buffer grows dynamically and transparently, so user need not explicitly request
 * it, though it is possible and advised when more messages are expected.
 */
class message_parser
{
    std::vector<uint8_t> buffer_;

    // The index of the beginning of the current message, i.e. the beginning of the
    // message length field.
    int message_begin_ = 0;

    // This is first byte after the last message byte, so anything in the range
    // [unused_begin_, buffer_size()) is garbage.
    int unused_begin_ = 0;

public:
    bool is_full() const noexcept;
    bool empty() const noexcept;

    /** The number of message bytes in receive buffer. */
    int size() const noexcept;

    /** The number of bytes we can receive without reallocating the buffer, i.e.:
     * buffer_size() - size(). */
    int free_space_size() const noexcept;

    /**
     * The total number of bytes buffer can currently hold, i.e.:
     * size() + free_space_size().
     */
    int buffer_size() const noexcept;

    /**
     * Resizes `message_parser`'s internal receive buffer to accomodate `n` bytes. Note
     * this function merely reserves the physical space, but does not concern itself
     * whether there is actually `n` free bytes in the receive buffer after the
     * allocation. For this functionality, use `reserve_free_space`.
     */
    void reserve(const int n);

    /**
     * Ensures that there is free space to receive `n` bytes. Does nothing if the free
     * space is already large enough to accomodate `n` bytes.
     */
    void reserve_free_space(const int n);

    /**
     * If n is below unused_begin_, it will only shrink to unused_begin_, to preserve
     * unhandled messages.
     */
    void shrink_to_fit(const int n);

    /**
     * This returns the range of memory:
     * [unused_begin, unused_begin + min(n, free_space_size())
     * in which we can receive bytes. If there is no space for n bytes, space is
     * reserved, but note that no upper bound is enforced on the buffer size.
     */
    view<uint8_t> get_receive_buffer(const int n);

    /**
     * Records the number of bytes we managed to read (as it may not be the same amount
     * as was requested), so we need to tell parser where the actual data ends.
     */
    void record_received_bytes(const int n) noexcept;

    /** This method should be called before extracting the next message. */
    bool has_message() const noexcept;

    /**
     * Handshake strucure differs from regular message structure so this should be
     * called to check whether a handshake has been received.
     */
    bool has_handshake() const noexcept;

    /**
     * Handshake message structure differ from the other BT messages, so this method
     * must be used to extract them.
     *
     * An exception is thrown if the handshake could not be parsed.
     *
     * NOTE: no guarantees can be made that the current message is not a handshake,
     * (since the message header is different), so caller must ensure that they only
     * call this when it is known that the message is a handshake.
     */
    handshake extract_handshake();

    /**
     * Returns the next message and advances the "message pointer", i.e.
     * consecutive calls to this function convey iterator semantics.
     *
     * An exception is thrown if has_message() returns false.
     */
    message extract_message();

    /**
     * Same as above, except it doesn't advance the message pointer, so subsequent calls
     * always return the same message.
     *
     * An exception is thrown if has_message() returns false.
     */
    message view_message() const;

    /**
     * Returns the message id/type of the current message.
     * Semantically the same as calling peek().type, but more efficient (doesn't parse
     * the whole message). The message need not be complete to determine the type, it
     * only requires that the first 5 bytes, or 4 bytes for the keep alive message, be
     * present.
     *
     * An excpetion is thrown if the available bytes is less than 5, or 4, in the case of
     * a keep alive message.
     */
    int type() const;

    /**
     * This returns a view starting at the current message till one past the last valid
     * message byte in receive buffer, so it may be empty.
     */
    const_view<uint8_t> view_raw_bytes() const noexcept;

    /**
     * Returns the number of bytes left till the completion of the current message if
     * the message is incomplete (we only received a chunk of it), or 0 if it is. -1 is
     * returned if we don't even have the 4 four bytes to determine the message length.
     */
    int num_bytes_left_till_completion() const noexcept;

    /**
     * Skips to the next message. Exception is thrown if there is no message to skip.
     */
    void skip_message();

    /**
     * This should be called once all the messages in message buffer have been
     * extracted.
     * If extracting everything resulted in reaching the end of the buffer, the
     * message pointer is simply reset to 0. If, on the other hand, we have an
     * incomplete message, we verify that the buffer is large enough to receive the
     * rest, reserve space if it's not, and then shift the incomplete bytes to the
     * front to increase the receive space.
     * TODO we might not want to automatically reserve space
     */
    void optimize_receive_space();

private:
    /**
     * Determines if we have n more message bytes in buffer in the range
     * [message_begin_, message_begin_ + n).
     */
    bool has(const int n) const noexcept;

    /**
     * Returns the current message's length but does not advance message_begin_.
     * The return value is the same as the message length field in the BitTorrent
     * message. That is, it only counts the content length, excluding the message length
     * field itself (which is 4 bytes).
     */
    int view_message_length() const noexcept;

    /**
     * Shift last message to the front of the buffer so that we can maximize the free
     * receive space (because after the last valid message the message parser will most
     * likely be filled with new messages).
     *
     * from:
     *
     *  wasted space     receive space
     *  vvvvvvvvvvv      vvvvv
     *  -----------======-----
     *             ^- incomplete message
     *
     * to:
     *        receive space
     *        vvvvvvvvvvvvvvvv
     *  ======----------------
     */
    void shift_last_message_to_front();
};

inline bool message_parser::empty() const noexcept
{
    return size() == 0;
}

inline bool message_parser::is_full() const noexcept
{
    return size() == buffer_size();
}

inline int message_parser::size() const noexcept
{
    return unused_begin_;
}

inline int message_parser::free_space_size() const noexcept
{
    return buffer_size() - size();
}

inline int message_parser::buffer_size() const noexcept
{
    return buffer_.size();
}

} // namespace tide

#endif // TIDE_SEND_BUFFER_HEADER
