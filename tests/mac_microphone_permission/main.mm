#include "streaming/macpermissions.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QThread>
#include <QTimer>
#include <cstdio>
#import <AVFoundation/AVFoundation.h>
#import <CoreFoundation/CoreFoundation.h>
#import <objc/runtime.h>

static AVAuthorizationStatus status = AVAuthorizationStatusNotDetermined;
static void (^pending)(BOOL) = nil;
static int requests = 0;

static AVAuthorizationStatus fakeStatus(id, SEL, NSString*)
{
    return status;
}

static void fakeRequest(id, SEL, NSString*, void (^completion)(BOOL))
{
    ++requests;
    pending = Block_copy(completion);
}

static void answer(BOOL granted)
{
    pending(granted);
    Block_release(pending);
    pending = nil;
}

static void pump()
{
    QCoreApplication::processEvents();
    CFRunLoopRunInMode(kCFRunLoopDefaultMode, 0.03, false);
    QCoreApplication::processEvents();
}

int main(int argc, char** argv)
{
    @autoreleasepool {
        QCoreApplication app(argc, argv);
        // Replace only this test process's permission boundary; never contact TCC or capture audio.
        Method getter = class_getClassMethod([AVCaptureDevice class],
                                             @selector(authorizationStatusForMediaType:));
        Method requester = class_getClassMethod([AVCaptureDevice class], @selector
                                                (requestAccessForMediaType:completionHandler:));
        if (!getter || !requester) {
            return 1;
        }
        IMP originalGetter = method_setImplementation(getter, (IMP)fakeStatus);
        IMP originalRequester = method_setImplementation(requester, (IMP)fakeRequest);
        bool ok = true;
        auto check = [&](bool condition, const char* message) {
            if (!condition) {
                std::fprintf(stderr, "FAIL: %s\n", message);
                ok = false;
            }
        };
        QObject owner;
        bool called = false, granted = false, onMain = false, heartbeat = false;
        QElapsedTimer timer;
        timer.start();
        requestMicrophonePermission(&owner, [&](bool allowed) {
            called = true;
            granted = allowed;
            onMain = QThread::currentThread() == app.thread();
        });
        check(timer.elapsed() < 500 && pending && !called,
              "permission request must return without waiting");
        QTimer::singleShot(0, &owner, [&] { heartbeat = true; });
        pump();
        check(heartbeat && !called, "GUI must remain responsive while the user has not answered");
        answer(YES);
        pump();
        check(called && granted && onMain, "grant must complete on the application thread");

        auto* abandoned = new QObject;
        bool abandonedCalled = false;
        requestMicrophonePermission(abandoned, [&](bool) { abandonedCalled = true; });
        delete abandoned;
        answer(NO);
        pump();
        check(!abandonedCalled, "permission result must not access a destroyed owner");

        status = AVAuthorizationStatusDenied;
        const int previousRequests = requests;
        called = false;
        requestMicrophonePermission(&owner, [&](bool allowed) {
            called = true;
            granted = allowed;
        });
        pump();
        check(called && !granted && requests == previousRequests,
              "denied permission must not reopen a system request");
        check(!isMicrophonePermissionGranted(), "denied status must remain denied");
        status = AVAuthorizationStatusAuthorized;
        check(isMicrophonePermissionGranted(), "authorized status must be recognized");
        method_setImplementation(getter, originalGetter);
        method_setImplementation(requester, originalRequester);
        if (ok) {
            std::puts("mac_microphone_permission=passed");
        }
        return ok ? 0 : 1;
    }
}
