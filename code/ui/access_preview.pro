QT += widgets
CONFIG += c++17 release
TARGET = access_preview
CODE = $$clean_path($$PWD/..)
ROOT = $$clean_path($$CODE/..)
INCLUDEPATH += $$CODE $$ROOT
SOURCES += $$CODE/ui/main.cpp $$CODE/ui/pin_dialog.cpp $$CODE/ui/user_panel.cpp
HEADERS += $$CODE/ui/pin_dialog.h $$CODE/ui/user_panel.h
include(core.pri)
QMAKE_CXXFLAGS += -Wall -Wextra -pthread
LIBS += -lcrypto -pthread
