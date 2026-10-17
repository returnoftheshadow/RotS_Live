/* background_line_reader.h */

#ifndef BACKGROUND_LINE_READER_H
#define BACKGROUND_LINE_READER_H

#include <istream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace harness {

// Reads lines from a stream on a thread of its own and hands them to the thread that owns the
// reader. Every member function runs on the owner's thread.
class BackgroundLineReader {
public:
    // Starts reading input on the reader's thread. Nothing else may use input until it ends or
    // fails, and it must stay valid until then, even after the reader is destroyed.
    explicit BackgroundLineReader(std::istream& input);

    // Joins the reading thread if input has already ended. Otherwise detaches it: a thread
    // blocked reading a stream cannot be interrupted portably, so it finishes when input ends or
    // fails.
    ~BackgroundLineReader();

    // A copy would share the reading thread.
    BackgroundLineReader(const BackgroundLineReader&) = delete;
    BackgroundLineReader& operator=(const BackgroundLineReader&) = delete;

    // Removes and returns the lines read since the last call, oldest first, without their
    // newlines. A line arrives whole, however long it is.
    [[nodiscard]] std::vector<std::string> take_lines();

    // True once input has ended or failed; no line arrives after those already read.
    [[nodiscard]] bool input_closed() const;

private:
    // What the reading thread shares with the reader. Shared ownership lets a detached reading
    // thread keep using it after the reader is destroyed.
    struct ReceivedInput {
        // Guards lines and closed.
        std::mutex guard;
        // Lines read and not yet taken, oldest first.
        std::vector<std::string> lines;
        // True once input has ended or failed.
        bool closed = false;
    };

    // Reads lines from input into received until input ends or fails, then marks it closed.
    static void read_lines(std::istream& input, std::shared_ptr<ReceivedInput> received);

    // The lines and end-of-input state the reading thread fills.
    std::shared_ptr<ReceivedInput> received_input;
    // Runs read_lines; declared last so it starts after the members it uses.
    std::thread reading_thread;
};

} // namespace harness

#endif /* BACKGROUND_LINE_READER_H */
