// macpermissions_stub.cpp - Stub for non-macOS platforms
#include "macpermissions.h"
#include <QObject>

bool isMicrophonePermissionGranted()
{
    // No permission pre-check needed on non-macOS platforms
    return true;
}

void requestMicrophonePermission(QObject* context, std::function<void(bool)> completion)
{
    QMetaObject::invokeMethod(context, [completion] { completion(true); }, Qt::QueuedConnection);
}
