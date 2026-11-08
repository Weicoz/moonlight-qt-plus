#pragma once

#include <QCoreApplication>
#include <QPointer>
#include <QQmlEngine>
#include <functional>
#include <utility>

namespace SessionCleanup {
inline void retainUntilCompletion(QObject* owner)
{
    owner->setParent(QCoreApplication::instance());
    QQmlEngine::setObjectOwnership(owner, QQmlEngine::CppOwnership);
}

inline void postCompletion(QObject* owner, std::function<void()> completion)
{
    QMetaObject::invokeMethod(
        QCoreApplication::instance(),
        [owner = QPointer<QObject>(owner), completion = std::move(completion)] {
            if (owner) {
                completion();
            }
        },
        Qt::QueuedConnection);
}
} // namespace SessionCleanup
