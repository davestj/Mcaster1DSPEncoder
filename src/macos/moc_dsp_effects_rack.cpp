/****************************************************************************
** Meta object code from reading C++ file 'dsp_effects_rack.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "dsp_effects_rack.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'dsp_effects_rack.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3mc114DspEffectsRackE_t {};
} // unnamed namespace

template <> constexpr inline auto mc1::DspEffectsRack::qt_create_metaobjectdata<qt_meta_tag_ZN3mc114DspEffectsRackE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "mc1::DspEffectsRack",
        "openEq10",
        "",
        "openEq31",
        "openSonic",
        "openPttDuck",
        "openAgc",
        "openDbxVoice",
        "dspToggleChanged",
        "statusMessage",
        "msg"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'openEq10'
        QtMocHelpers::SignalData<void()>(1, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'openEq31'
        QtMocHelpers::SignalData<void()>(3, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'openSonic'
        QtMocHelpers::SignalData<void()>(4, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'openPttDuck'
        QtMocHelpers::SignalData<void()>(5, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'openAgc'
        QtMocHelpers::SignalData<void()>(6, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'openDbxVoice'
        QtMocHelpers::SignalData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'dspToggleChanged'
        QtMocHelpers::SignalData<void()>(8, 2, QMC::AccessPublic, QMetaType::Void),
        // Signal 'statusMessage'
        QtMocHelpers::SignalData<void(const QString &)>(9, 2, QMC::AccessPublic, QMetaType::Void, {{
            { QMetaType::QString, 10 },
        }}),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<DspEffectsRack, qt_meta_tag_ZN3mc114DspEffectsRackE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject mc1::DspEffectsRack::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3mc114DspEffectsRackE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3mc114DspEffectsRackE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3mc114DspEffectsRackE_t>.metaTypes,
    nullptr
} };

void mc1::DspEffectsRack::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<DspEffectsRack *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->openEq10(); break;
        case 1: _t->openEq31(); break;
        case 2: _t->openSonic(); break;
        case 3: _t->openPttDuck(); break;
        case 4: _t->openAgc(); break;
        case 5: _t->openDbxVoice(); break;
        case 6: _t->dspToggleChanged(); break;
        case 7: _t->statusMessage((*reinterpret_cast<std::add_pointer_t<QString>>(_a[1]))); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (DspEffectsRack::*)()>(_a, &DspEffectsRack::openEq10, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (DspEffectsRack::*)()>(_a, &DspEffectsRack::openEq31, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (DspEffectsRack::*)()>(_a, &DspEffectsRack::openSonic, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (DspEffectsRack::*)()>(_a, &DspEffectsRack::openPttDuck, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (DspEffectsRack::*)()>(_a, &DspEffectsRack::openAgc, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (DspEffectsRack::*)()>(_a, &DspEffectsRack::openDbxVoice, 5))
            return;
        if (QtMocHelpers::indexOfMethod<void (DspEffectsRack::*)()>(_a, &DspEffectsRack::dspToggleChanged, 6))
            return;
        if (QtMocHelpers::indexOfMethod<void (DspEffectsRack::*)(const QString & )>(_a, &DspEffectsRack::statusMessage, 7))
            return;
    }
}

const QMetaObject *mc1::DspEffectsRack::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *mc1::DspEffectsRack::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3mc114DspEffectsRackE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int mc1::DspEffectsRack::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 8)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 8;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 8)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 8;
    }
    return _id;
}

// SIGNAL 0
void mc1::DspEffectsRack::openEq10()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void mc1::DspEffectsRack::openEq31()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void mc1::DspEffectsRack::openSonic()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void mc1::DspEffectsRack::openPttDuck()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void mc1::DspEffectsRack::openAgc()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void mc1::DspEffectsRack::openDbxVoice()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}

// SIGNAL 6
void mc1::DspEffectsRack::dspToggleChanged()
{
    QMetaObject::activate(this, &staticMetaObject, 6, nullptr);
}

// SIGNAL 7
void mc1::DspEffectsRack::statusMessage(const QString & _t1)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 7, nullptr, _t1);
}
QT_WARNING_POP
