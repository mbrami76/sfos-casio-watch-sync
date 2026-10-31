// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Michele Brami

#include "watchproto.h"

#include <stdio.h>

namespace watchproto {

namespace {

const uint8_t kTimeFeature = 0x09;           // R-TIME-6
const size_t kTimestampChars = 15;           // yyyyMMddTHHmmss

bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int daysInMonth(int y, int m)
{
    static const int d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (m == 2 && isLeap(y)) ? 29 : d[m - 1];
}

void putU16(Bytes &b, unsigned v)
{
    b.push_back(uint8_t(v & 0xff));
    b.push_back(uint8_t((v >> 8) & 0xff));
}

void putU32(Bytes &b, uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        b.push_back(uint8_t((v >> (8 * i)) & 0xff));
}

void putString(Bytes &b, const std::string &s)
{
    putU16(b, unsigned(s.size()));
    b.insert(b.end(), s.begin(), s.end());
}

bool formatTimestamp(const DateTime &dt, std::string *out)
{
    if (validateDateTime(dt) != FieldNone)
        return false;
    char buf[32];
    snprintf(buf, sizeof(buf), "%04d%02d%02dT%02d%02d%02d", dt.year, dt.month,
             dt.day, dt.hour, dt.minute, dt.second);
    *out = buf;
    return true;
}

bool parseTimestamp(const uint8_t *p, DateTime *dt)
{
    for (size_t i = 0; i < kTimestampChars; ++i) {
        if (i == 8 ? p[i] != 'T' : (p[i] < '0' || p[i] > '9'))
            return false;
    }
    int v[6]; // yyyy MM dd HH mm ss
    const size_t off[6] = {0, 4, 6, 9, 11, 13};
    const size_t len[6] = {4, 2, 2, 2, 2, 2};
    for (int k = 0; k < 6; ++k) {
        v[k] = 0;
        for (size_t i = 0; i < len[k]; ++i)
            v[k] = v[k] * 10 + (p[off[k] + i] - '0');
    }
    DateTime t(v[0], v[1], v[2], v[3], v[4], v[5]);
    if (validateDateTime(t) != FieldNone)
        return false;
    *dt = t;
    return true;
}

// Length of the valid UTF-8 sequence starting at s[i], or 0 if there is none.
size_t sequenceLength(const std::string &s, size_t i)
{
    const uint8_t b0 = uint8_t(s[i]);
    size_t n;
    uint8_t lo = 0x80, hi = 0xBF; // allowed range of the second byte
    if (b0 < 0x80) return 1;
    else if (b0 >= 0xC2 && b0 <= 0xDF) n = 2;
    else if (b0 >= 0xE0 && b0 <= 0xEF) {
        n = 3;
        if (b0 == 0xE0) lo = 0xA0;      // no overlong forms
        if (b0 == 0xED) hi = 0x9F;      // no surrogates
    } else if (b0 >= 0xF0 && b0 <= 0xF4) {
        n = 4;
        if (b0 == 0xF0) lo = 0x90;
        if (b0 == 0xF4) hi = 0x8F;      // nothing above U+10FFFF
    } else {
        return 0;
    }
    if (i + n > s.size()) return 0;
    const uint8_t b1 = uint8_t(s[i + 1]);
    if (b1 < lo || b1 > hi) return 0;
    for (size_t k = 2; k < n; ++k) {
        const uint8_t c = uint8_t(s[i + k]);
        if (c < 0x80 || c > 0xBF) return 0;
    }
    return n;
}

} // namespace

// ---------------------------------------------------------------- date-time

Field validateDateTime(const DateTime &dt)
{
    if (dt.year < kMinYear || dt.year > kMaxYear) return FieldYear;
    if (dt.month < 1 || dt.month > 12) return FieldMonth;
    if (dt.day < 1 || dt.day > daysInMonth(dt.year, dt.month)) return FieldDay;
    if (dt.hour < 0 || dt.hour > 23) return FieldHour;
    if (dt.minute < 0 || dt.minute > 59) return FieldMinute;
    if (dt.second < 0 || dt.second > 59) return FieldSecond; // no leap second
    return FieldNone;
}

int dayOfWeek(int year, int month, int day, DowEncoding enc)
{
    static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
    const int y = month < 3 ? year - 1 : year;
    const int sun0 = (y + y / 4 - y / 100 + y / 400 + t[month - 1] + day) % 7;
    return (enc == DowMon1 && sun0 == 0) ? 7 : sun0;
}

TimeFrame buildTimeFrame(const DateTime &dt, DowEncoding enc)
{
    TimeFrame tf;
    tf.invalidField = validateDateTime(dt);
    if (tf.invalidField != FieldNone)
        return tf;
    Bytes &b = tf.bytes;
    b.push_back(kTimeFeature);
    putU16(b, unsigned(dt.year));
    b.push_back(uint8_t(dt.month));
    b.push_back(uint8_t(dt.day));
    b.push_back(uint8_t(dt.hour));
    b.push_back(uint8_t(dt.minute));
    b.push_back(uint8_t(dt.second));
    b.push_back(uint8_t(dayOfWeek(dt.year, dt.month, dt.day, enc)));
    b.push_back(0x00);
    b.push_back(0x01);
    return tf;
}

bool decodeTimeFrame(const Bytes &f, DateTime *dt, int *dow)
{
    if (f.size() != kTimeFrameSize || f[0] != kTimeFeature || f[9] != 0x00 || f[10] != 0x01)
        return false;
    DateTime t(f[1] | (f[2] << 8), f[3], f[4], f[5], f[6], f[7]);
    if (validateDateTime(t) != FieldNone || f[8] > 7)
        return false;
    if (dt) *dt = t;
    if (dow) *dow = f[8];
    return true;
}

// --------------------------------------------------------------------- UTF-8

bool isValidUtf8(const std::string &s)
{
    for (size_t i = 0; i < s.size();) {
        const size_t n = sequenceLength(s, i);
        if (n == 0) return false;
        i += n;
    }
    return true;
}

size_t countCodePoints(const std::string &s)
{
    size_t count = 0;
    for (size_t i = 0; i < s.size();) {
        const size_t n = sequenceLength(s, i);
        i += n ? n : 1;
        ++count;
    }
    return count;
}

std::string truncateUtf8(const std::string &s, size_t maxChars, size_t maxBytes)
{
    size_t i = 0, chars = 0;
    while (i < s.size() && chars < maxChars) {
        const size_t n = sequenceLength(s, i);
        if (n == 0 || i + n > maxBytes) break;
        i += n;
        ++chars;
    }
    return s.substr(0, i);
}

// ------------------------------------------------------------- notifications

Bytes invertBytes(const Bytes &b)
{
    Bytes r(b);
    for (size_t i = 0; i < r.size(); ++i)
        r[i] = uint8_t(~r[i]);
    return r;
}

std::string normaliseText(const std::string &utf8)
{
    std::string r;
    r.reserve(utf8.size());
    for (size_t i = 0; i < utf8.size(); ++i) {
        char c = utf8[i];
        if (c == '\r') {
            r += '\n';
            if (i + 1 < utf8.size() && utf8[i + 1] == '\n')
                ++i;
        } else if (c != '\t') {
            r += c;
        }
    }
    return r;
}

Notification fitToWatch(const Notification &n)
{
    Notification r(n);
    std::string *fields[kFieldCount] = {&r.sender, &r.title, &r.subtitle, &r.message};

    // Only the frame size limits the text: notifications may be multi-line and scroll on the watch,
    // so there is no display line budget.
    size_t room = kMaxFrameBytes - kMinFrameSize;
    for (size_t k = 0; k < kFieldCount; ++k) {
        std::string &f = *fields[k];
        f = normaliseText(f);
        f = truncateUtf8(f, f.size(), room);
        room -= f.size();
    }
    return r;
}

NotifFrame buildNotificationFrame(const Notification &n)
{
    NotifFrame r;
    std::string ts;
    if (n.category > kMaxCategory) { r.error = NotifBadCategory; return r; }
    if (!formatTimestamp(n.timestamp, &ts)) { r.error = NotifBadTimestamp; return r; }
    const std::string *fields[kFieldCount] = {&n.sender, &n.title, &n.subtitle, &n.message};
    size_t size = kMinFrameSize;
    for (size_t k = 0; k < kFieldCount; ++k) {
        if (!isValidUtf8(*fields[k])) { r.error = NotifBadUtf8; return r; }
        size += fields[k]->size();
    }
    if (size > kMaxFrameBytes) { r.error = NotifTooLarge; return r; }

    Bytes b;
    b.reserve(size);
    putU32(b, n.id);
    b.push_back(kFlagNone);
    b.push_back(n.alert ? 1 : 0);
    b.push_back(n.category);
    b.insert(b.end(), ts.begin(), ts.end());
    for (size_t k = 0; k < kFieldCount; ++k)
        putString(b, *fields[k]);
    r.bytes = invertBytes(b);
    return r;
}

NotifFrame buildDeleteFrame(uint32_t id, const DateTime &timestamp)
{
    NotifFrame r;
    std::string ts;
    if (!formatTimestamp(timestamp, &ts)) { r.error = NotifBadTimestamp; return r; }
    Bytes b;
    putU32(b, id);
    b.push_back(kFlagDelete);
    b.push_back(0); // alert
    b.push_back(0); // category
    b.insert(b.end(), ts.begin(), ts.end());
    for (size_t k = 0; k < kFieldCount; ++k)
        putU16(b, 0);
    r.bytes = invertBytes(b);
    return r;
}

bool decodeFrame(const Bytes &wire, DecodedFrame *out)
{
    if (wire.size() < kMinFrameSize)
        return false;
    const Bytes p = invertBytes(wire);
    DecodedFrame d;
    d.id = uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
    d.deleteFlag = p[4];
    d.alert = p[5];
    d.category = p[6];
    if (!parseTimestamp(&p[7], &d.timestamp))
        return false;

    std::string *fields[kFieldCount] = {&d.sender, &d.title, &d.subtitle, &d.message};
    size_t pos = kHeaderSize;
    for (size_t k = 0; k < kFieldCount; ++k) {
        if (pos + 2 > p.size()) return false;
        const size_t len = size_t(p[pos]) | (size_t(p[pos + 1]) << 8);
        pos += 2;
        if (len > p.size() - pos) return false;
        fields[k]->assign(p.begin() + pos, p.begin() + pos + len);
        if (!isValidUtf8(*fields[k])) return false;
        pos += len;
    }
    if (pos != p.size())
        return false;
    if (out) *out = d;
    return true;
}

} // namespace watchproto
