#pragma once

// The firmware version, in one place. It goes out in Info (docs/protocol.md
// §3.1) and in the console's `stat`, and it is what the server compares an
// available release against, so it has to be bumped in the same commit as the
// change it names.
//
//   patch — a fix nothing else can see
//   minor — anything the phone or the server can observe: a new opcode, a new
//           Info field, a changed default
//   major — a break: an older bridge app stops working
//
// 0.2.0 is the first version there is; everything before it predates the
// firmware knowing its own name, and reports itself as Info version 1.

#define LC_VERSION_MAJOR 0
#define LC_VERSION_MINOR 2
#define LC_VERSION_PATCH 0

// Built from the numbers rather than typed out again, so the two can't drift.
#define LC_STRINGIFY_(x) #x
#define LC_STRINGIFY(x) LC_STRINGIFY_(x)
#define LC_VERSION_STRING \
    LC_STRINGIFY(LC_VERSION_MAJOR) "." LC_STRINGIFY(LC_VERSION_MINOR) "." LC_STRINGIFY(LC_VERSION_PATCH)
