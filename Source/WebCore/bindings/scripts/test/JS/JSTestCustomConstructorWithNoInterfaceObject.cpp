/*
    This file is part of the WebKit open source project.
    This file has been generated by generate-bindings.pl. DO NOT MODIFY!

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Library General Public
    License as published by the Free Software Foundation; either
    version 2 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Library General Public License for more details.

    You should have received a copy of the GNU Library General Public License
    along with this library; see the file COPYING.LIB.  If not, write to
    the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
    Boston, MA 02110-1301, USA.
*/

#include "config.h"
#include "JSTestCustomConstructorWithNoInterfaceObject.h"

#include "JSDOMBinding.h"
#include "JSDOMConstructor.h"
#include "JSDOMExceptionHandling.h"
#include "JSDOMWrapperCache.h"
#include <JavaScriptCore/FunctionPrototype.h>
#include <JavaScriptCore/JSCInlines.h>
#include <wtf/GetPtr.h>
#include <wtf/PointerPreparations.h>


namespace WebCore {
using namespace JSC;

// Attributes

JSC::EncodedJSValue jsTestCustomConstructorWithNoInterfaceObjectConstructor(JSC::ExecState*, JSC::EncodedJSValue, JSC::PropertyName);
bool setJSTestCustomConstructorWithNoInterfaceObjectConstructor(JSC::ExecState*, JSC::EncodedJSValue, JSC::EncodedJSValue);

class JSTestCustomConstructorWithNoInterfaceObjectPrototype : public JSC::JSNonFinalObject {
public:
    using Base = JSC::JSNonFinalObject;
    static JSTestCustomConstructorWithNoInterfaceObjectPrototype* create(JSC::VM& vm, JSDOMGlobalObject* globalObject, JSC::Structure* structure)
    {
        JSTestCustomConstructorWithNoInterfaceObjectPrototype* ptr = new (NotNull, JSC::allocateCell<JSTestCustomConstructorWithNoInterfaceObjectPrototype>(vm.heap)) JSTestCustomConstructorWithNoInterfaceObjectPrototype(vm, globalObject, structure);
        ptr->finishCreation(vm);
        return ptr;
    }

    DECLARE_INFO;
    static JSC::Structure* createStructure(JSC::VM& vm, JSC::JSGlobalObject* globalObject, JSC::JSValue prototype)
    {
        return JSC::Structure::create(vm, globalObject, prototype, JSC::TypeInfo(JSC::ObjectType, StructureFlags), info());
    }

private:
    JSTestCustomConstructorWithNoInterfaceObjectPrototype(JSC::VM& vm, JSC::JSGlobalObject*, JSC::Structure* structure)
        : JSC::JSNonFinalObject(vm, structure)
    {
    }

    void finishCreation(JSC::VM&);
};

using JSTestCustomConstructorWithNoInterfaceObjectConstructor = JSDOMConstructor<JSTestCustomConstructorWithNoInterfaceObject>;

template<> JSC::EncodedJSValue JSC_HOST_CALL JSTestCustomConstructorWithNoInterfaceObjectConstructor::construct(JSC::ExecState* exec)
{
    ASSERT(exec);
    return constructJSTestCustomConstructorWithNoInterfaceObject(*exec);
}

template<> JSValue JSTestCustomConstructorWithNoInterfaceObjectConstructor::prototypeForStructure(JSC::VM& vm, const JSDOMGlobalObject& globalObject)
{
    UNUSED_PARAM(vm);
    return globalObject.functionPrototype();
}

template<> void JSTestCustomConstructorWithNoInterfaceObjectConstructor::initializeProperties(VM& vm, JSDOMGlobalObject& globalObject)
{
    putDirect(vm, vm.propertyNames->prototype, JSTestCustomConstructorWithNoInterfaceObject::prototype(vm, globalObject), JSC::PropertyAttribute::DontDelete | JSC::PropertyAttribute::ReadOnly | JSC::PropertyAttribute::DontEnum);
    putDirect(vm, vm.propertyNames->name, jsNontrivialString(&vm, String(ASCIILiteral("TestCustomConstructorWithNoInterfaceObject"))), JSC::PropertyAttribute::ReadOnly | JSC::PropertyAttribute::DontEnum);
    putDirect(vm, vm.propertyNames->length, jsNumber(0), JSC::PropertyAttribute::ReadOnly | JSC::PropertyAttribute::DontEnum);
}

template<> const ClassInfo JSTestCustomConstructorWithNoInterfaceObjectConstructor::s_info = { "TestCustomConstructorWithNoInterfaceObject", &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSTestCustomConstructorWithNoInterfaceObjectConstructor) };

/* Hash table for prototype */

static const HashTableValue JSTestCustomConstructorWithNoInterfaceObjectPrototypeTableValues[] =
{
    { "constructor", static_cast<unsigned>(JSC::PropertyAttribute::DontEnum), NoIntrinsic, { (intptr_t)static_cast<PropertySlot::GetValueFunc>(jsTestCustomConstructorWithNoInterfaceObjectConstructor), (intptr_t) static_cast<PutPropertySlot::PutValueFunc>(setJSTestCustomConstructorWithNoInterfaceObjectConstructor) } },
};

const ClassInfo JSTestCustomConstructorWithNoInterfaceObjectPrototype::s_info = { "TestCustomConstructorWithNoInterfaceObjectPrototype", &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSTestCustomConstructorWithNoInterfaceObjectPrototype) };

void JSTestCustomConstructorWithNoInterfaceObjectPrototype::finishCreation(VM& vm)
{
    Base::finishCreation(vm);
    reifyStaticProperties(vm, JSTestCustomConstructorWithNoInterfaceObject::info(), JSTestCustomConstructorWithNoInterfaceObjectPrototypeTableValues, *this);
}

const ClassInfo JSTestCustomConstructorWithNoInterfaceObject::s_info = { "TestCustomConstructorWithNoInterfaceObject", &Base::s_info, nullptr, nullptr, CREATE_METHOD_TABLE(JSTestCustomConstructorWithNoInterfaceObject) };

JSTestCustomConstructorWithNoInterfaceObject::JSTestCustomConstructorWithNoInterfaceObject(Structure* structure, JSDOMGlobalObject& globalObject, Ref<TestCustomConstructorWithNoInterfaceObject>&& impl)
    : JSDOMWrapper<TestCustomConstructorWithNoInterfaceObject>(structure, globalObject, WTFMove(impl))
{
}

void JSTestCustomConstructorWithNoInterfaceObject::finishCreation(VM& vm)
{
    Base::finishCreation(vm);
    ASSERT(inherits(vm, info()));

}

JSObject* JSTestCustomConstructorWithNoInterfaceObject::createPrototype(VM& vm, JSDOMGlobalObject& globalObject)
{
    return JSTestCustomConstructorWithNoInterfaceObjectPrototype::create(vm, &globalObject, JSTestCustomConstructorWithNoInterfaceObjectPrototype::createStructure(vm, &globalObject, globalObject.objectPrototype()));
}

JSObject* JSTestCustomConstructorWithNoInterfaceObject::prototype(VM& vm, JSDOMGlobalObject& globalObject)
{
    return getDOMPrototype<JSTestCustomConstructorWithNoInterfaceObject>(vm, globalObject);
}

void JSTestCustomConstructorWithNoInterfaceObject::destroy(JSC::JSCell* cell)
{
    JSTestCustomConstructorWithNoInterfaceObject* thisObject = static_cast<JSTestCustomConstructorWithNoInterfaceObject*>(cell);
    thisObject->JSTestCustomConstructorWithNoInterfaceObject::~JSTestCustomConstructorWithNoInterfaceObject();
}

EncodedJSValue jsTestCustomConstructorWithNoInterfaceObjectConstructor(ExecState* state, EncodedJSValue thisValue, PropertyName)
{
    VM& vm = state->vm();
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    auto* prototype = jsDynamicCast<JSTestCustomConstructorWithNoInterfaceObjectPrototype*>(vm, JSValue::decode(thisValue));
    if (UNLIKELY(!prototype))
        return throwVMTypeError(state, throwScope);
    JSValue constructor = JSTestCustomConstructorWithNoInterfaceObjectConstructor::create(state->vm(), JSTestCustomConstructorWithNoInterfaceObjectConstructor::createStructure(state->vm(), *prototype->globalObject(), prototype->globalObject()->objectPrototype()), *jsCast<JSDOMGlobalObject*>(prototype->globalObject()));
    // Shadowing constructor property to ensure reusing the same constructor object
    prototype->putDirect(vm, vm.propertyNames->constructor, constructor, JSC::PropertyAttribute::DontEnum | JSC::PropertyAttribute::ReadOnly);
    return JSValue::encode(constructor);
}

bool setJSTestCustomConstructorWithNoInterfaceObjectConstructor(ExecState* state, EncodedJSValue thisValue, EncodedJSValue encodedValue)
{
    VM& vm = state->vm();
    auto throwScope = DECLARE_THROW_SCOPE(vm);
    auto* prototype = jsDynamicCast<JSTestCustomConstructorWithNoInterfaceObjectPrototype*>(vm, JSValue::decode(thisValue));
    if (UNLIKELY(!prototype)) {
        throwVMTypeError(state, throwScope);
        return false;
    }
    // Shadowing a built-in constructor
    return prototype->putDirect(vm, vm.propertyNames->constructor, JSValue::decode(encodedValue));
}

bool JSTestCustomConstructorWithNoInterfaceObjectOwner::isReachableFromOpaqueRoots(JSC::Handle<JSC::Unknown> handle, void*, SlotVisitor& visitor)
{
    UNUSED_PARAM(handle);
    UNUSED_PARAM(visitor);
    return false;
}

void JSTestCustomConstructorWithNoInterfaceObjectOwner::finalize(JSC::Handle<JSC::Unknown> handle, void* context)
{
    auto* jsTestCustomConstructorWithNoInterfaceObject = static_cast<JSTestCustomConstructorWithNoInterfaceObject*>(handle.slot()->asCell());
    auto& world = *static_cast<DOMWrapperWorld*>(context);
    uncacheWrapper(world, &jsTestCustomConstructorWithNoInterfaceObject->wrapped(), jsTestCustomConstructorWithNoInterfaceObject);
}

#if ENABLE(BINDING_INTEGRITY)
#if PLATFORM(WIN)
#pragma warning(disable: 4483)
extern "C" { extern void (*const __identifier("??_7TestCustomConstructorWithNoInterfaceObject@WebCore@@6B@")[])(); }
#else
extern "C" { extern void* _ZTVN7WebCore42TestCustomConstructorWithNoInterfaceObjectE[]; }
#endif
#endif

JSC::JSValue toJSNewlyCreated(JSC::ExecState*, JSDOMGlobalObject* globalObject, Ref<TestCustomConstructorWithNoInterfaceObject>&& impl)
{

#if ENABLE(BINDING_INTEGRITY)
    void* actualVTablePointer = *(reinterpret_cast<void**>(impl.ptr()));
#if PLATFORM(WIN)
    void* expectedVTablePointer = WTF_PREPARE_VTBL_POINTER_FOR_INSPECTION(__identifier("??_7TestCustomConstructorWithNoInterfaceObject@WebCore@@6B@"));
#else
    void* expectedVTablePointer = WTF_PREPARE_VTBL_POINTER_FOR_INSPECTION(&_ZTVN7WebCore42TestCustomConstructorWithNoInterfaceObjectE[2]);
#endif

    // If this fails TestCustomConstructorWithNoInterfaceObject does not have a vtable, so you need to add the
    // ImplementationLacksVTable attribute to the interface definition
    static_assert(std::is_polymorphic<TestCustomConstructorWithNoInterfaceObject>::value, "TestCustomConstructorWithNoInterfaceObject is not polymorphic");

    // If you hit this assertion you either have a use after free bug, or
    // TestCustomConstructorWithNoInterfaceObject has subclasses. If TestCustomConstructorWithNoInterfaceObject has subclasses that get passed
    // to toJS() we currently require TestCustomConstructorWithNoInterfaceObject you to opt out of binding hardening
    // by adding the SkipVTableValidation attribute to the interface IDL definition
    RELEASE_ASSERT(actualVTablePointer == expectedVTablePointer);
#endif
    return createWrapper<TestCustomConstructorWithNoInterfaceObject>(globalObject, WTFMove(impl));
}

JSC::JSValue toJS(JSC::ExecState* state, JSDOMGlobalObject* globalObject, TestCustomConstructorWithNoInterfaceObject& impl)
{
    return wrap(state, globalObject, impl);
}

TestCustomConstructorWithNoInterfaceObject* JSTestCustomConstructorWithNoInterfaceObject::toWrapped(JSC::VM& vm, JSC::JSValue value)
{
    if (auto* wrapper = jsDynamicCast<JSTestCustomConstructorWithNoInterfaceObject*>(vm, value))
        return &wrapper->wrapped();
    return nullptr;
}

}