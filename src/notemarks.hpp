#pragma once
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "color.hpp"
#include "marks.hpp"
#include "score.hpp"

// A time within a bar: quarter notes from the barline, as a fraction (num / den, den > 0).
struct BarTime {
    int num = 0, den = 1;
};
bool operator==(BarTime, BarTime);  // the same time, however written: 1/2 == 2/4
std::string describeTime(BarTime);  // "3/2", or "2" for a whole number

// Reads a time: a whole number ("2"), a fraction ("3/2") or a decimal ("1.5"), from 0.
std::optional<BarTime> parseTime(std::string_view);

// Notes marked as part of a diff, from a marks file: the notes in bar `bar` (its printed number),
// narrowed down to those on `staff`, starting `at`, of `pitch` and in `voice`, where given.
struct NoteMark {
    Tone tone = Tone::Changed;
    int bar = 1;
    int staff = 0;                // 0: any staff
    std::optional<BarTime> at;    // when the notes start
    std::optional<int> pitch;     // the pitch they sound, as a MIDI number
    std::optional<int> voice;     // which voice of the staff
    std::string from;             // where it came from, for messages: "diff.json, mark 3"
};

// How a note mark is named in messages: "diff.json, mark 3 (changed bar 12, staff 2, at 3/2, pitch 66)".
std::string nameNoteMark(const NoteMark&);

// Reads a marks file: a JSON list of marks, each an object like
//
//     {"tone": "changed", "bar": 12, "staff": 2, "at": "3/2", "pitch": 66}
//
// with "tone" (added, changed or removed) and "bar" (a printed bar number) always, and any of:
//   "to"     the last bar of a range of whole bars, like --added 12-15
//   "staff"  one staff, counting from 1, top to bottom (otherwise every staff)
//   "at"     only the notes starting then: quarter notes from the barline, as a number or a string
//            ("3/2", or "1/3" in a triplet)
//   "pitch"  only the notes sounding this MIDI pitch
//   "voice"  only the notes in this voice
// A mark with "at", "pitch" or "voice" is of notes; one without is of whole bars, as --added and
// the like give. Appends them to `bars` and `notes`, named after `name` (the file's name), or on a
// mistake returns false with the reason in `error`, appending nothing.
bool parseMarksFile(std::string_view json, const std::string& name, std::vector<Mark>& bars,
                    std::vector<NoteMark>& notes, std::string& error);

// Where the changes start, for the pager's n and N keys (View::Stops::changes): the first bar of each
// run of marked bars (counting from 0, left to right), with the staff it's marked on, or 0 if it's
// marked on more than one. Bar marks must have been found by checkMarks.
std::vector<std::pair<int, int>> changeStarts(const std::vector<Mark>& bars, const std::vector<NoteMark>& notes,
                                              const BarNumbers&);

// What to draw in a tone for these note marks (for SvgDocument::setToneElements): each note they
// find, or a chord whose notes are all marked alike, so that its stem, dots and articulations go
// with them. Checks that each mark finds at least one note, that no note is marked two different
// ways, and that none is in a bar marked another way by `bars` (already found by checkMarks).
// Returns "" if all is well, otherwise what's wrong with the first mark that isn't.
std::string findNoteMarks(const std::vector<NoteMark>&, const std::vector<ScoreNote>&, const ScoreLayout&,
                          const BarNumbers&, const std::vector<Mark>& bars,
                          std::vector<std::pair<std::string, Tone>>& elements);
