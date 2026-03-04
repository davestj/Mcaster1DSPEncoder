/****************************************************************************
** Meta object code from reading C++ file 'effects_rack_tab.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "effects_rack_tab.h"
#include <QtGui/qtextcursor.h>
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'effects_rack_tab.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3mc114EffectsRackTabE_t {};
} // unnamed namespace

template <> constexpr inline auto mc1::EffectsRackTab::qt_create_metaobjectdata<qt_meta_tag_ZN3mc114EffectsRackTabE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "mc1::EffectsRackTab",
        "openEq10",
        "",
        "openEq31",
        "openSonic",
        "openPttDuck",
        "openAgc",
        "openDbxVoice"
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
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<EffectsRackTab, qt_meta_tag_ZN3mc114EffectsRackTabE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject mc1::EffectsRackTab::staticMetaObject = { {
    QMetaObject::SuperData::link<QWidget::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3mc114EffectsRackTabE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3mc114EffectsRackTabE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3mc114EffectsRackTabE_t>.metaTypes,
    nullptr
} };

void mc1::EffectsRackTab::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<EffectsRackTab *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->openEq10(); break;
        case 1: _t->openEq31(); break;
        case 2: _t->openSonic(); break;
        case 3: _t->openPttDuck(); break;
        case 4: _t->openAgc(); break;
        case 5: _t->openDbxVoice(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (EffectsRackTab::*)()>(_a, &EffectsRackTab::openEq10, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (EffectsRackTab::*)()>(_a, &EffectsRackTab::openEq31, 1))
            return;
        if (QtMocHelpers::indexOfMethod<void (EffectsRackTab::*)()>(_a, &EffectsRackTab::openSonic, 2))
            return;
        if (QtMocHelpers::indexOfMethod<void (EffectsRackTab::*)()>(_a, &EffectsRackTab::openPttDuck, 3))
            return;
        if (QtMocHelpers::indexOfMethod<void (EffectsRackTab::*)()>(_a, &EffectsRackTab::openAgc, 4))
            return;
        if (QtMocHelpers::indexOfMethod<void (EffectsRackTab::*)()>(_a, &EffectsRackTab::openDbxVoice, 5))
            return;
    }
}

const QMetaObject *mc1::EffectsRackTab::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *mc1::EffectsRackTab::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3mc114EffectsRackTabE_t>.strings))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int mc1::EffectsRackTab::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 6)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 6;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 6)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 6;
    }
    return _id;
}

// SIGNAL 0
void mc1::EffectsRackTab::openEq10()
{
    QMetaObject::activate(this, &staticMetaObject, 0, nullptr);
}

// SIGNAL 1
void mc1::EffectsRackTab::openEq31()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}

// SIGNAL 2
void mc1::EffectsRackTab::openSonic()
{
    QMetaObject::activate(this, &staticMetaObject, 2, nullptr);
}

// SIGNAL 3
void mc1::EffectsRackTab::openPttDuck()
{
    QMetaObject::activate(this, &staticMetaObject, 3, nullptr);
}

// SIGNAL 4
void mc1::EffectsRackTab::openAgc()
{
    QMetaObject::activate(this, &staticMetaObject, 4, nullptr);
}

// SIGNAL 5
void mc1::EffectsRackTab::openDbxVoice()
{
    QMetaObject::activate(this, &staticMetaObject, 5, nullptr);
}
QT_WARNING_POP
