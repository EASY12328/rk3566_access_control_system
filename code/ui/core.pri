# The core is standard C++17 and Linux APIs.
SOURCES += $$CODE/app/access_app.cpp \
           $$CODE/services/door_service.cpp $$CODE/services/face_service.cpp \
           $$CODE/services/pin_store.cpp $$CODE/services/user_store.cpp \
           $$CODE/platform/bmp.cpp $$CODE/platform/crypto.cpp $$CODE/platform/files.cpp \
           $$CODE/platform/process.cpp $$CODE/drivers/camera.cpp $$CODE/drivers/servo_lock.cpp \
           $$ROOT/third_party/cjson/cJSON.c
