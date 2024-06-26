// Copyright (c) 2021 -  Stefan de Bruijn
// Use of this source code is governed by a GPLv3 license that can be found in the LICENSE file.

#include "Config.h"
#include "Protocol.h"
#include "Serial.h"
#include "SettingsDefinitions.h"

EnumItem messageLevels2[] = {
    { MsgLevelNone, "None" }, { MsgLevelError, "Error" }, { MsgLevelWarning, "Warn" },
    { MsgLevelInfo, "Info" }, { MsgLevelDebug, "Debug" }, { MsgLevelVerbose, "Verbose" },
};

bool atMsgLevel(MsgLevel level) {
    return message_level == nullptr || message_level->get() >= level;
}

LogStream::LogStream(Channel& channel, MsgLevel level, const char* name) : _channel(channel), _level(level) {
    _line = new std::string();
    print(name);
}

LogStream::LogStream(Channel& channel, const char* name) : LogStream(channel, MsgLevelNone, name) {}
LogStream::LogStream(MsgLevel level, const char* name) : LogStream(allChannels, level, name) {}

size_t LogStream::write(uint8_t c) {
    *_line += (char)c;
    return 1;
}

LogStream::~LogStream() {
    if ((*_line).length() && (*_line)[0] == '[') {
        *_line += ']';
    }
    send_line(_channel, _level, _line);
}
