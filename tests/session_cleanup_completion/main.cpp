#include "streaming/sessioncleanupcompletion.h"

#include <QCoreApplication>
#include <QThread>
#include <QThreadPool>
#include <QTextStream>
#include <QEvent>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QThreadPool pool;
    QObject owner;
    bool completed = false;
    bool mainThread = false;

    pool.start([&] {
        SessionCleanup::postCompletion(&owner, [&] {
            completed = true;
            mainThread = QThread::currentThread() == app.thread();
        });
    });
    // Reproduce app.exec() returning before main waits for the thread pool.
    // A worker must finish even while the GUI event loop is stopped.
    const bool drainedWithoutGui = pool.waitForDone(1000);
    QCoreApplication::sendPostedEvents();
    pool.waitForDone();
    if (!drainedWithoutGui || !completed || !mainThread) {
        QTextStream(stderr) << "FAIL: cleanup worker depends on the stopped GUI event loop\n";
        return 1;
    }

    bool deletedOwnerCalled = false;
    auto* deletedOwner = new QObject;
    pool.start(
        [&] { SessionCleanup::postCompletion(deletedOwner, [&] { deletedOwnerCalled = true; }); });
    pool.waitForDone();
    delete deletedOwner;
    QCoreApplication::sendPostedEvents();
    if (deletedOwnerCalled) {
        QTextStream(stderr) << "FAIL: completion ran after its session was destroyed\n";
        return 1;
    }

    // QML teardown must not collect a session still owned by a cleanup worker.
    QPointer<QObject> retained = new QObject;
    {
        QQmlEngine engine;
        QQmlEngine::setObjectOwnership(retained, QQmlEngine::JavaScriptOwnership);
        QJSValue wrapper = engine.newQObject(retained);
        SessionCleanup::retainUntilCompletion(retained);
    }
    if (!retained) {
        QTextStream(stderr) << "FAIL: QML teardown destroyed an active cleanup owner\n";
        return 1;
    }
    pool.start([&] { SessionCleanup::postCompletion(retained, [&] { retained->deleteLater(); }); });
    if (!pool.waitForDone(1000)) {
        QCoreApplication::sendPostedEvents();
        pool.waitForDone();
        QTextStream(stderr) << "FAIL: retained session cleanup blocked on the GUI\n";
        return 1;
    }
    QCoreApplication::sendPostedEvents();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    if (retained) {
        QTextStream(stderr) << "FAIL: finished cleanup owner was not released\n";
        return 1;
    }

    QTextStream(stdout) << "session_cleanup_completion=passed\n";
    return 0;
}
