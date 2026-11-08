// Request permission before touching CoreAudio, without blocking the application thread.

#include "macpermissions.h"

#include <QPointer>

#import <AVFoundation/AVFoundation.h>
#import <dispatch/dispatch.h>

bool isMicrophonePermissionGranted()
{
    return [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio] ==
           AVAuthorizationStatusAuthorized;
}

void requestMicrophonePermission(QObject* context, std::function<void(bool)> completion)
{
    const QPointer<QObject> owner(context);
    auto deliver = [owner, completion](bool granted) {
        dispatch_async(dispatch_get_main_queue(), ^{
          if (owner) {
              completion(granted);
          }
        });
    };
    const auto status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
    if (status == AVAuthorizationStatusNotDetermined) {
        [AVCaptureDevice requestAccessForMediaType:AVMediaTypeAudio
                                 completionHandler:^(BOOL g) {
                                   deliver(g);
                                 }];
    } else {
        deliver(status == AVAuthorizationStatusAuthorized);
    }
}
