// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

// watchproto: builds and validates the frames written to the watch. Pure logic, no I/O, no Qt.
// Facts:; requirements R-TIME-6, R-TIME-7, R-NOTIF-5, R-NOTIF-6.
#ifndef WATCHPROTO_H
#define WATCHPROTO_H

#include <stddef.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace watchproto {

typedef std::vector<uint8_t> Bytes;

// ---------------------------------------------------------------- date-time

struct DateTime {
    int year, month, day, hour, minute, second;
    DateTime() : year(0), month(0), day(0), hour(0), minute(0), second(0) {}
    DateTime(int y, int mo, int d, int h, int mi, int s)
        : year(y), month(mo), day(d), hour(h), minute(mi), second(s) {}
};

enum Field {
    FieldNone = 0,  // all fields valid
    FieldYear,
    FieldMonth,
    FieldDay,
    FieldHour,
    FieldMinute,
    FieldSecond
};

// [assumption] The watch's year range is unknown; stay in 2000..2099.
const int kMinYear = 2000;
const int kMaxYear = 2099;

// First invalid field in the order year, month, day, hour, minute, second
// (day is checked against the real calendar, leap years included), or FieldNone.
Field validateDateTime(const DateTime &dt);

// Day-of-week byte encoding, a per-model parameter (R-TIME-7, R-GEN-3).
enum DowEncoding {
    DowSun0,  // Sunday=0 ... Saturday=6 (default)
    DowMon1   // Monday=1 ... Sunday=7
};

// Weekday of a valid date in the given encoding.
int dayOfWeek(int year, int month, int day, DowEncoding enc);

const size_t kTimeFrameSize = 11;

struct TimeFrame {
    Bytes bytes;        // empty on error
    Field invalidField; // FieldNone on success
    bool ok() const { return invalidField == FieldNone; }
    TimeFrame() : invalidField(FieldNone) {}
};

// Feature 0x09 frame (R-TIME-6): 09 | year LE | month | day | hour | minute |
// second | dow | 00 | 01. Every field is validated; on error the field is named.
TimeFrame buildTimeFrame(const DateTime &dt, DowEncoding enc = DowSun0);

// Parses and validates an 11-byte time frame. `dow` receives the raw byte.
bool decodeTimeFrame(const Bytes &frame, DateTime *dt, int *dow);

// --------------------------------------------------------------------- UTF-8

// Strict check: no overlong forms, no surrogates, nothing above U+10FFFF.
bool isValidUtf8(const std::string &s);

// Number of code points in a valid UTF-8 string.
size_t countCodePoints(const std::string &s);

// Longest prefix of at most maxChars code points and at most maxBytes bytes,
// cut only on character boundaries. Invalid UTF-8 ends the text at the first
// bad sequence, so the result is always valid.
std::string truncateUtf8(const std::string &s, size_t maxChars, size_t maxBytes);

// ------------------------------------------------------------- notifications

// Frame layout: 22-byte header, four fields with a 2-byte LE length each, then every byte
// inverted.
const size_t kHeaderSize = 22;
const size_t kFieldCount = 4;
const size_t kMinFrameSize = kHeaderSize + 2 * kFieldCount; // 30: delete frame
const size_t kDeleteFrameSize = kMinFrameSize;

// 214 B was accepted and 286 B refused (MTU of 0030 is 250); the real limit is
// presumably 247 B but unverified, so the largest verified size is the bound.
const size_t kMaxFrameBytes = 214;

const uint8_t kFlagNone = 0x00;
const uint8_t kFlagDelete = 0x02;

// Categories seen on the watch; 1-3 and 7-13 were not tested. [assumption] 13 is the highest valid
// category.
const uint8_t kCategoryGeneric = 0;  // check mark in a circle
const uint8_t kCategoryChat = 4;     // chat bubble
const uint8_t kCategoryCalendar = 5;
const uint8_t kCategoryEmail = 6;    // envelope
const uint8_t kMaxCategory = 13;

enum NotifError {
    NotifOk = 0,
    NotifBadCategory,
    NotifBadTimestamp,
    NotifBadUtf8,
    NotifTooLarge       // frame would exceed kMaxFrameBytes
};

struct Notification {
    uint32_t id;
    bool alert;          // true: pops up and vibrates; false: list only
    uint8_t category;
    DateTime timestamp;
    std::string sender, title, subtitle, message; // UTF-8
    Notification() : id(0), alert(false), category(0) {}
};

struct NotifFrame {
    Bytes bytes;         // the wire bytes (already inverted); empty on error
    NotifError error;
    bool ok() const { return error == NotifOk; }
    NotifFrame() : error(NotifOk) {}
};

// Text normalisation verified on the watch: "\r\n" and a lone "\r" become "\n", "\t" is dropped,
// every other byte is unchanged (invalid UTF-8 is not its job). Idempotent.
std::string normaliseText(const std::string &utf8);

// Normalises the four fields (normaliseText), then truncates them so the frame fits kMaxFrameBytes
// (the budget is measured on the normalised text), on UTF-8 boundaries, filling in the order
// sender, title, subtitle, message. There is no limit on lines or characters: newlines are kept and
// the text may scroll on the watch. Leaves id, alert, category and timestamp untouched.
Notification fitToWatch(const Notification &n);

// Validates and encodes. Does not truncate: an oversize frame is an error
// (call fitToWatch first). Returns the inverted wire bytes.
NotifFrame buildNotificationFrame(const Notification &n);

// Delete frame: same id, flag 0x02, alert 0, category 0, four empty fields.
NotifFrame buildDeleteFrame(uint32_t id, const DateTime &timestamp);

// Inverts every byte (its own inverse).
Bytes invertBytes(const Bytes &b);

struct DecodedFrame {
    uint32_t id;
    uint8_t deleteFlag, alert, category; // raw bytes
    DateTime timestamp;
    std::string sender, title, subtitle, message;
    DecodedFrame() : id(0), deleteFlag(0), alert(0), category(0) {}
};

// Parses wire bytes (inverts them first). False if the layout is inconsistent:
// too short, lengths not adding up exactly, bad timestamp, invalid UTF-8.
bool decodeFrame(const Bytes &wire, DecodedFrame *out);

} // namespace watchproto

#endif // WATCHPROTO_H
