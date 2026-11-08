#pragma once

#include <QDeadlineTimer>
#include <QList>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>
#include <utility>

class ComputerPollingEntry
{
public:
    ComputerPollingEntry() : m_ActiveThread(nullptr) {}

    virtual ~ComputerPollingEntry()
    {
        interrupt();

        // interrupt() should have taken care of this
        Q_ASSERT(m_ActiveThread == nullptr);

        for (QThread* thread : std::as_const(m_InactiveList)) {
            thread->wait();
            delete thread;
        }
    }

    bool isActive()
    {
        cleanInactiveList();

        return m_ActiveThread != nullptr;
    }

    void setActiveThread(QThread* thread)
    {
        cleanInactiveList();

        Q_ASSERT(!isActive());
        m_ActiveThread = thread;
    }

    void interrupt()
    {
        cleanInactiveList();

        if (m_ActiveThread != nullptr) {
            // Interrupt the active thread
            {
                QMutexLocker locker(&m_WaitMutex);
                m_ActiveThread->requestInterruption();
                m_WaitCondition.wakeAll();
            }

            // Place it on the inactive list awaiting death
            m_InactiveList.append(m_ActiveThread);

            m_ActiveThread = nullptr;
        }
    }

    void waitForNextPoll()
    {
        QMutexLocker locker(&m_WaitMutex);
        QDeadlineTimer deadline(3000);
        while (!QThread::currentThread()->isInterruptionRequested() && !deadline.hasExpired()) {
            m_WaitCondition.wait(&m_WaitMutex, deadline);
        }
    }

private:
    void cleanInactiveList()
    {
        QMutableListIterator<QThread*> i(m_InactiveList);

        // Reap any threads that have finished
        while (i.hasNext()) {
            i.next();

            QThread* thread = i.value();
            if (thread->isFinished()) {
                delete thread;
                i.remove();
            }
        }
    }

    QMutex m_WaitMutex;
    QWaitCondition m_WaitCondition;
    QThread* m_ActiveThread;
    QList<QThread*> m_InactiveList;
};
