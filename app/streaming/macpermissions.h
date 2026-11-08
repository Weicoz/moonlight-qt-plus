#pragma once

#include <functional>
class QObject;

bool isMicrophonePermissionGranted();
void requestMicrophonePermission(QObject* context, std::function<void(bool)> completion);
