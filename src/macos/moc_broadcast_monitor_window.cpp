/****************************************************************************
** Meta object code from reading C++ file 'broadcast_monitor_window.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "video/broadcast_monitor_window.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'broadcast_monitor_window.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 69
#error "This file was generated using the moc from 6.10.2. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

#ifndef Q_CONSTINIT
#define Q_CONSTINIT
#endif

QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
QT_WARNING_DISABLE_GCC("-Wuseless-cast")
namespace {
struct qt_meta_tag_ZN3mc122BroadcastMonitorWindowE_t {};
} // unnamed namespace

template <> constexpr inline auto mc1::BroadcastMonitorWindow::qt_create_metaobjectdata<qt_meta_tag_ZN3mc122BroadcastMonitorWindowE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "mc1::BroadcastMonitorWindow",
        "windowHidden",
        "",
        "goLiveRequested",
        "stopRequested",
        "dryRunToggled",
        "active",
        "onGoLive",
        "onStop",
        "onDryRun",
        "updateDuration",
        "updateOnAirBlink"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'windowHidden'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'goLiveRequested'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'stopRequested'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'dryRunToggled'
        QtMocHelpers::SignalData<void(bool)>(5, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::Bool, 6 },
        }}),
        // Slot 'onGoLive'
        QtMocHelpers::SlotData<void()>(7, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onStop'
        QtMocHelpers::SlotData<void()>(8, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDryRun'
        QtMocHelpers::SlotData<void()>(9, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'updateDuration'
        QtMocHelpers::SlotData<void()>(10, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'updateOnAirBlink'
        QtMocHelpers::SlotData<void()>(11, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<BroadcastMonitorWindow, qt_meta_tag_ZN3mc122BroadcastMonitorWindowE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject mc1::BroadcastMonitorWindow::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3mc122BroadcastMonitorWindowE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3mc122BroadcastMonitorWindowE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3mc122BroadcastMonitorWindowE_t>.metaTypes,
    nullptr
} };

void mc1::BroadcastMonitorWindow::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<BroadcastMonitorWindow *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->windowHidden(); break;
        case 1: _t->goLiveRequested(); break;
        case 2: _t->stopRequested(); break;
        case 3: _t->dryRunToggled((*reinterpret_cast<std::add_pointer_t<bool>>(_a[1]))); break;
        case 4: _t->onGoLive(); break;
        case 5: _t->onStop(); break;
        case 6: _t->onDryRun(); break;
        case 7: _t->updateDuration(); break;
        case 8: _t->updateOnAirBlink(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (BroadcastMonitorWindow::*)()>(_a, &BroadcastMonitorWindow::windowHidden, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (BroadcastMonitorWindow::*)()>(_a, &BroadcastMonitorWindow::goLiveRequested, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (BroadcastMonitorWindow::*)()>(_a, &BroadcastMonitorWindow::stopRequested, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (BroadcastMonitorWindow::*)(bool )>(_a, &BroadcastMonitorWindow::dryRunToggled, 3))
            return;
    }
}

const QMetaObject *mc1::BroadcastMonitorWindow::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *mc1::BroadcastMonitorWindow::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3mc122BroadcastMonitorWindowE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int mc1::BroadcastMonitorWindow::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 9)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 9;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 9)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 9;
    }
    return _id;
}

// SIGNAL 0
void mc1::BroadcastMonitorWindow::windowHidden()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void mc1::BroadcastMonitorWindow::goLiveRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void mc1::BroadcastMonitorWindow::stopRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void mc1::BroadcastMonitorWindow::dryRunToggled(bool _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 3, nullptr, _t1);
}
QT_WARNING_POP
