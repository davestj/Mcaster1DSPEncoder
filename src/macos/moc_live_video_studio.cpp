/****************************************************************************
** Meta object code from reading C++ file 'live_video_studio.h'
**
** Created by: The Qt Meta Object Compiler version 69 (Qt 6.10.2)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "video/live_video_studio.h"
#include <QtCore/qmetatype.h>

#include <QtCore/qtmochelpers.h>

#include <memory>


#include <QtCore/qxptype_traits.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'live_video_studio.h' doesn't include <QObject>."
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
struct qt_meta_tag_ZN3mc121LiveVideoStudioDialogE_t {};
} // unnamed namespace

template <> constexpr inline auto mc1::LiveVideoStudioDialog::qt_create_metaobjectdata<qt_meta_tag_ZN3mc121LiveVideoStudioDialogE_t>()
{
    namespace QMC = QtMocConstants;
    QtMocHelpers::StringRefStorage qt_stringData {
        "mc1::LiveVideoStudioDialog",
        "goLiveRequested",
        "",
        "VideoConfig",
        "vcfg",
        "StreamTargetEntry",
        "target",
        "stopRequested",
        "onCodecChanged",
        "index",
        "onAddTarget",
        "onEditTarget",
        "onRemoveTarget",
        "onGoLive",
        "onStop",
        "onDryRun",
        "updateDuration",
        "onTransition",
        "onTransitionTick",
        "onTransitionDurationChanged",
        "value",
        "onAddSource",
        "onRemoveSource",
        "onSourceDoubleClicked"
    };

    QtMocHelpers::UintData qt_methods {
        // Signal 'goLiveRequested'
        QtMocHelpers::SignalData<void(const VideoConfig &, const StreamTargetEntry &)>(1, 2, QMC::AccessPublic, QMetaType::Void, {{
            { 0x80000000 | 3, 4 }, { 0x80000000 | 5, 6 },
        }}),
        // Signal 'stopRequested'
        QtMocHelpers::SignalData<void()>(7, 2, QMC::AccessPublic, QMetaType::Void),
        // Slot 'onCodecChanged'
        QtMocHelpers::SlotData<void(int)>(8, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 9 },
        }}),
        // Slot 'onAddTarget'
        QtMocHelpers::SlotData<void()>(10, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onEditTarget'
        QtMocHelpers::SlotData<void()>(11, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onRemoveTarget'
        QtMocHelpers::SlotData<void()>(12, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onGoLive'
        QtMocHelpers::SlotData<void()>(13, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onStop'
        QtMocHelpers::SlotData<void()>(14, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onDryRun'
        QtMocHelpers::SlotData<void()>(15, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'updateDuration'
        QtMocHelpers::SlotData<void()>(16, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onTransition'
        QtMocHelpers::SlotData<void()>(17, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onTransitionTick'
        QtMocHelpers::SlotData<void()>(18, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onTransitionDurationChanged'
        QtMocHelpers::SlotData<void(int)>(19, 2, QMC::AccessPrivate, QMetaType::Void, {{
            { QMetaType::Int, 20 },
        }}),
        // Slot 'onAddSource'
        QtMocHelpers::SlotData<void()>(21, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onRemoveSource'
        QtMocHelpers::SlotData<void()>(22, 2, QMC::AccessPrivate, QMetaType::Void),
        // Slot 'onSourceDoubleClicked'
        QtMocHelpers::SlotData<void()>(23, 2, QMC::AccessPrivate, QMetaType::Void),
    };
    QtMocHelpers::UintData qt_properties {
    };
    QtMocHelpers::UintData qt_enums {
    };
    return QtMocHelpers::metaObjectData<LiveVideoStudioDialog, qt_meta_tag_ZN3mc121LiveVideoStudioDialogE_t>(QMC::MetaObjectFlag{}, qt_stringData,
            qt_methods, qt_properties, qt_enums);
}
Q_CONSTINIT const QMetaObject mc1::LiveVideoStudioDialog::staticMetaObject = { {
    QMetaObject::SuperData::link<QDialog::staticMetaObject>(),
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3mc121LiveVideoStudioDialogE_t>.stringdata,
    qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3mc121LiveVideoStudioDialogE_t>.data,
    qt_static_metacall,
    nullptr,
    qt_staticMetaObjectRelocatingContent<qt_meta_tag_ZN3mc121LiveVideoStudioDialogE_t>.metaTypes,
    nullptr
} };

void mc1::LiveVideoStudioDialog::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    auto *_t = static_cast<LiveVideoStudioDialog *>(_o);
    if (_c == QMetaObject::InvokeMetaMethod) {
        switch (_id) {
        case 0: _t->goLiveRequested((*reinterpret_cast<std::add_pointer_t<VideoConfig>>(_a[1])),(*reinterpret_cast<std::add_pointer_t<StreamTargetEntry>>(_a[2]))); break;
        case 1: _t->stopRequested(); break;
        case 2: _t->onCodecChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 3: _t->onAddTarget(); break;
        case 4: _t->onEditTarget(); break;
        case 5: _t->onRemoveTarget(); break;
        case 6: _t->onGoLive(); break;
        case 7: _t->onStop(); break;
        case 8: _t->onDryRun(); break;
        case 9: _t->updateDuration(); break;
        case 10: _t->onTransition(); break;
        case 11: _t->onTransitionTick(); break;
        case 12: _t->onTransitionDurationChanged((*reinterpret_cast<std::add_pointer_t<int>>(_a[1]))); break;
        case 13: _t->onAddSource(); break;
        case 14: _t->onRemoveSource(); break;
        case 15: _t->onSourceDoubleClicked(); break;
        default: ;
        }
    }
    if (_c == QMetaObject::IndexOfMethod) {
        if (QtMocHelpers::indexOfMethod<void (LiveVideoStudioDialog::*)(const VideoConfig & , const StreamTargetEntry & )>(_a, &LiveVideoStudioDialog::goLiveRequested, 0))
            return;
        if (QtMocHelpers::indexOfMethod<void (LiveVideoStudioDialog::*)()>(_a, &LiveVideoStudioDialog::stopRequested, 1))
            return;
    }
}

const QMetaObject *mc1::LiveVideoStudioDialog::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *mc1::LiveVideoStudioDialog::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_staticMetaObjectStaticContent<qt_meta_tag_ZN3mc121LiveVideoStudioDialogE_t>.strings))
        return static_cast<void*>(this);
    return QDialog::qt_metacast(_clname);
}

int mc1::LiveVideoStudioDialog::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QDialog::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 16)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 16;
    }
    if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 16)
            *reinterpret_cast<QMetaType *>(_a[0]) = QMetaType();
        _id -= 16;
    }
    return _id;
}

// SIGNAL 0
void mc1::LiveVideoStudioDialog::goLiveRequested(const VideoConfig & _t1, const StreamTargetEntry & _t2)
{
    QMetaObject::activate<void>(this, &staticMetaObject, 0, nullptr, _t1, _t2);
}

// SIGNAL 1
void mc1::LiveVideoStudioDialog::stopRequested()
{
    QMetaObject::activate(this, &staticMetaObject, 1, nullptr);
}
QT_WARNING_POP
