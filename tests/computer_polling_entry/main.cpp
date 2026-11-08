#include "backend/computerpollingentry.h"

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QSemaphore>
#include <QTextStream>

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    QTextStream err(stderr);
    bool ok = true;
    auto require = [&](bool condition, const char* message) {
        if (!condition) {
            err << "FAIL: " << message << '\n';
            ok = false;
        }
    };

    // Normal polling keeps the existing three-second interval.
    {
        ComputerPollingEntry entry;
        qint64 elapsedMs = 0;
        auto* worker = QThread::create([&] {
            QElapsedTimer timer;
            timer.start();
            entry.waitForNextPoll();
            elapsedMs = timer.elapsed();
        });
        entry.setActiveThread(worker);
        worker->start();
        if (!worker->wait(6000)) {
            require(false, "normal poll wait must finish");
            return 1;
        }
        require(elapsedMs >= 2900, "normal poll wait must not spin or poll early");
    }

    // Cancellation before entering the wait must not lose the wakeup.
    {
        ComputerPollingEntry entry;
        QSemaphore started, proceed;
        auto* worker = QThread::create([&] {
            started.release();
            proceed.acquire();
            entry.waitForNextPoll();
        });
        entry.setActiveThread(worker);
        worker->start();
        started.acquire();
        entry.interrupt();
        proceed.release();
        if (!worker->wait(1000)) {
            require(false, "cancellation before wait must finish promptly");
            return 1;
        }
    }

    // Repeated start/stop must still reap finished threads and permit reuse.
    {
        ComputerPollingEntry entry;
        for (int i = 0; i < 30; ++i) {
            QSemaphore started;
            auto* worker = QThread::create([&] {
                started.release();
                entry.waitForNextPoll();
            });
            entry.setActiveThread(worker);
            worker->start();
            started.acquire();
            QThread::msleep(5);
            entry.interrupt();
            if (!worker->wait(1000)) {
                require(false, "cancellation during wait must finish promptly");
                return 1;
            }
            require(!entry.isActive(), "interrupted entry must accept a new worker");
        }
    }

    // An entry can start a replacement before the interrupted worker has exited.
    {
        ComputerPollingEntry entry;
        QSemaphore oldStarted, releaseOld, newStarted;
        auto* oldWorker = QThread::create([&] {
            oldStarted.release();
            releaseOld.acquire();
            entry.waitForNextPoll();
        });
        entry.setActiveThread(oldWorker);
        oldWorker->start();
        oldStarted.acquire();
        entry.interrupt();
        auto* newWorker = QThread::create([&] {
            newStarted.release();
            entry.waitForNextPoll();
        });
        entry.setActiveThread(newWorker);
        newWorker->start();
        newStarted.acquire();
        releaseOld.release();
        if (!oldWorker->wait(1000)) {
            require(false, "replaced worker must retain its cancellation");
            return 1;
        }
        entry.interrupt();
        if (!newWorker->wait(1000)) {
            require(false, "replacement worker must also be interruptible");
            return 1;
        }
    }

    if (ok) {
        QTextStream(stdout) << "computer_polling_entry=passed\n";
    }
    return ok ? 0 : 1;
}
