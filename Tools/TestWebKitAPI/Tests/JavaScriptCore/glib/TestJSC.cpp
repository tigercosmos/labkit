/*
 * Copyright (C) 2018 Igalia S.L.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2,1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public License
 * along with this library; see the file COPYING.LIB.  If not, write to
 * the Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "config.h"

// Include JSCContextPrivate.h to be able to run garbage collector for testing.
#define JSC_COMPILATION 1
#include "jsc/JSCContextPrivate.h"
#undef JSC_COMPILATION

#include <jsc/jsc.h>
#include <wtf/HashSet.h>
#include <wtf/Threading.h>
#include <wtf/Vector.h>
#include <wtf/glib/GRefPtr.h>
#include <wtf/glib/GUniquePtr.h>

class LeakChecker {
public:
    LeakChecker() = default;

    ~LeakChecker()
    {
        if (m_watchedObjects.isEmpty())
            return;

        g_print("Leaked objects:");
        for (auto* object : m_watchedObjects)
            g_print(" [%s(%p) %u refs]", g_type_name_from_instance(reinterpret_cast<GTypeInstance*>(object)), object, G_OBJECT(object)->ref_count);
        g_print("\n");
        g_assert_true(m_watchedObjects.isEmpty());
    }

    void watch(void* object)
    {
        g_assert_true(G_IS_OBJECT(object));
        m_watchedObjects.add(object);
        g_object_weak_ref(G_OBJECT(object), [](gpointer userData, GObject* object) {
            static_cast<LeakChecker*>(userData)->m_watchedObjects.remove(object);
        }, this);
    }

private:
    HashSet<void*> m_watchedObjects;
};

class ExceptionHandler {
public:
    ExceptionHandler(JSCContext* context)
        : m_context(context)
    {
        jsc_context_push_exception_handler(m_context, [](JSCContext*, JSCException* exception, gpointer) {
            g_error("UNEXPECTED EXCEPTION: %s", jsc_exception_get_message(exception));
        }, nullptr, nullptr);
    }

    ~ExceptionHandler()
    {
        pop();
    }

    void push(JSCExceptionHandler handler, gpointer userData, GDestroyNotify destroyFunction = nullptr)
    {
        jsc_context_push_exception_handler(m_context, handler, userData, destroyFunction);
    }

    void pop()
    {
        jsc_context_pop_exception_handler(m_context);
    }

private:
    JSCContext* m_context;
};

#define g_assert_throw_begin(handler, didThrow) \
    didThrow = false; \
    handler.push([](JSCContext*, JSCException*, gpointer userData) { \
        *static_cast<bool*>(userData) = true; \
    }, &didThrow, nullptr);

#define g_assert_did_throw(handler, didThrow) \
    handler.pop(); \
    g_assert_true(didThrow); \
    didThrow = false;

extern "C" void JSSynchronousGarbageCollectForDebugging(JSContextRef);

static void jscContextGarbageCollect(JSCContext* context)
{
    JSSynchronousGarbageCollectForDebugging(jscContextGetJSContext(context));
}

static void testJSCBasic()
{
    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        auto* defaultVM = jsc_context_get_virtual_machine(context.get());
        g_assert_nonnull(defaultVM);
        checker.watch(defaultVM);

        GRefPtr<JSCVirtualMachine> vm = adoptGRef(jsc_virtual_machine_new());
        checker.watch(vm.get());
        g_assert_false(vm.get() == defaultVM);

        GRefPtr<JSCContext> context2 = adoptGRef(jsc_context_new_with_virtual_machine(vm.get()));
        checker.watch(context2.get());
        ExceptionHandler exceptionHandler2(context.get());
        g_assert_true(jsc_context_get_virtual_machine(context2.get()) == vm.get());

        GRefPtr<JSCContext> context3 = adoptGRef(jsc_context_new_with_virtual_machine(defaultVM));
        checker.watch(context3.get());
        ExceptionHandler exceptionHandler3(context.get());
        g_assert_true(jsc_context_get_virtual_machine(context3.get()) == jsc_context_get_virtual_machine(context.get()));

        GRefPtr<JSCValue> value1 = adoptGRef(jsc_value_new_number(context.get(), 25));
        checker.watch(value1.get());
        g_assert_true(jsc_value_get_context(value1.get()) == context.get());
        g_assert_true(jsc_value_is_number(value1.get()));
        g_assert_cmpint(jsc_value_to_int32(value1.get()), ==, 25);
        jsc_context_set_value(context.get(), "value1", value1.get());

        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "value1", -1));
        checker.watch(result.get());
        g_assert_true(result.get() == value1.get());

        result = adoptGRef(jsc_context_evaluate(context2.get(), "value1", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_get_context(result.get()) == context2.get());
        g_assert_true(jsc_value_is_undefined(result.get()));

        result = adoptGRef(jsc_context_get_value(context3.get(), "value1"));
        checker.watch(result.get());
        g_assert_true(jsc_value_get_context(result.get()) == context3.get());
        g_assert_true(jsc_value_is_undefined(result.get()));

        jsc_context_set_value(context3.get(), "value1", value1.get());
        result = adoptGRef(jsc_context_evaluate(context3.get(), "value1", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_get_context(result.get()) == context3.get());
        g_assert_false(result.get() == value1.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpint(jsc_value_to_int32(result.get()), ==, 25);
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "2 + 2", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_get_context(result.get()) == context.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpint(jsc_value_to_int32(result.get()), ==, 4);

        GRefPtr<JSCValue> result2 = adoptGRef(jsc_context_evaluate(context.get(), "2 + 2", -1));
        checker.watch(result2.get());
        g_assert_true(jsc_value_get_context(result2.get()) == context.get());
        g_assert_true(result.get() == result2.get());

        GRefPtr<JSCValue> result3 = adoptGRef(jsc_context_evaluate(context.get(), "3 + 1", -1));
        checker.watch(result3.get());
        g_assert_true(jsc_value_get_context(result3.get()) == context.get());
        g_assert_true(result.get() == result3.get());

        auto* resultPtr = result.get();
        result = nullptr;
        result2 = nullptr;
        result3 = nullptr;
        GRefPtr<JSCValue> result4 = adoptGRef(jsc_context_evaluate(context.get(), "2 + 2", -1));
        checker.watch(result4.get());
        g_assert_true(jsc_value_get_context(result4.get()) == context.get());
        g_assert_true(jsc_value_is_number(result4.get()));
        g_assert_cmpint(jsc_value_to_int32(result4.get()), ==, 4);
        g_assert_false(result4.get() == resultPtr);
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "2 + 2", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_get_context(result.get()) == context.get());
        jsc_context_set_value(context.get(), "four", result.get());

        GRefPtr<JSCValue> value = adoptGRef(jsc_context_get_value(context.get(), "four"));
        checker.watch(value.get());
        g_assert_true(jsc_value_get_context(value.get()) == context.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 4);
        g_assert_true(result.get() == value.get());

        value = adoptGRef(jsc_context_evaluate(context.get(), "four", -1));
        checker.watch(value.get());
        g_assert_true(result.get() == value.get());
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "two = 2; four = two + two", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_get_context(result.get()) == context.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpint(jsc_value_to_int32(result.get()), ==, 4);

        GRefPtr<JSCValue> value = adoptGRef(jsc_context_get_value(context.get(), "two"));
        checker.watch(value.get());
        g_assert_true(jsc_value_get_context(value.get()) == context.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 2);

        value = adoptGRef(jsc_context_get_value(context.get(), "four"));
        checker.watch(value.get());
        g_assert_true(result.get() == value.get());

        GRefPtr<JSCValue> result2 = adoptGRef(jsc_context_evaluate(context.get(), "five = 4", -1));
        checker.watch(result2.get());
        g_assert_true(result2.get() == value.get());
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        GUniquePtr<char> scriptFile(g_build_filename(WEBKIT_SRC_DIR, "Tools", "TestWebKitAPI", "Tests", "JavaScriptCore", "glib", "script.js", nullptr));
        GUniqueOutPtr<char> contents;
        gsize contentsSize;
        g_assert_true(g_file_get_contents(scriptFile.get(), &contents.outPtr(), &contentsSize, nullptr));
        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), contents.get(), contentsSize));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_undefined(result.get()));

        GString* expected = g_string_new("String");
        expected = g_string_append_c(expected, '\0');
        expected = g_string_append(expected, "With");
        expected = g_string_append_c(expected, '\0');
        expected = g_string_append(expected, "Null");
        GRefPtr<GBytes> expectedBytes = adoptGRef(g_string_free_to_bytes(expected));

        GRefPtr<JSCValue> value = adoptGRef(jsc_context_evaluate(context.get(), "testStringWithNull()", -1));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_string(value.get()));
        GUniquePtr<char> valueString(jsc_value_to_string(value.get()));
        g_assert_cmpstr(valueString.get(), ==, "String");
        GRefPtr<GBytes> valueBytes = adoptGRef(jsc_value_to_string_as_bytes(value.get()));
        g_assert_true(g_bytes_equal(valueBytes.get(), expectedBytes.get()));

        value = adoptGRef(jsc_value_new_string_from_bytes(context.get(), expectedBytes.get()));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_string(value.get()));
        valueString.reset(jsc_value_to_string(value.get()));
        g_assert_cmpstr(valueString.get(), ==, "String");
        valueBytes = adoptGRef(jsc_value_to_string_as_bytes(value.get()));
        g_assert_true(g_bytes_equal(valueBytes.get(), expectedBytes.get()));

        jsc_context_set_value(context.get(), "s", value.get());
        result = adoptGRef(jsc_context_get_value(context.get(), "s"));
        checker.watch(result.get());
        g_assert_true(result.get() == value.get());
    }
}

static void testJSCTypes()
{
    LeakChecker checker;
    GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
    checker.watch(context.get());
    ExceptionHandler exceptionHandler(context.get());

    GRefPtr<JSCValue> value = adoptGRef(jsc_value_new_number(context.get(), 125));
    checker.watch(value.get());
    g_assert_true(jsc_value_is_number(value.get()));
    g_assert_cmpfloat(jsc_value_to_double(value.get()), ==, 125.);
    g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 125);
    g_assert_true(jsc_value_to_boolean(value.get()) == TRUE);
    GUniquePtr<char> valueString(jsc_value_to_string(value.get()));
    g_assert_cmpstr(valueString.get(), ==, "125");
    jsc_context_set_value(context.get(), "i", value.get());
    GRefPtr<JSCValue> result = adoptGRef(jsc_context_get_value(context.get(), "i"));
    checker.watch(result.get());
    g_assert_true(result.get() == value.get());

    value = adoptGRef(jsc_value_new_number(context.get(), 12.5));
    checker.watch(value.get());
    g_assert_true(jsc_value_is_number(value.get()));
    g_assert_cmpfloat(jsc_value_to_double(value.get()), ==, 12.5);
    g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 12);
    g_assert_true(jsc_value_to_boolean(value.get()) == TRUE);
    valueString.reset(jsc_value_to_string(value.get()));
    g_assert_cmpstr(valueString.get(), ==, "12.5");
    jsc_context_set_value(context.get(), "d", value.get());
    result = adoptGRef(jsc_context_get_value(context.get(), "d"));
    checker.watch(result.get());
    g_assert_true(result.get() == value.get());

    value = adoptGRef(jsc_value_new_boolean(context.get(), TRUE));
    checker.watch(value.get());
    g_assert_true(jsc_value_is_boolean(value.get()));
    g_assert_true(jsc_value_to_boolean(value.get()) == TRUE);
    g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 1);
    valueString.reset(jsc_value_to_string(value.get()));
    g_assert_cmpstr(valueString.get(), ==, "true");
    jsc_context_set_value(context.get(), "b1", value.get());
    result = adoptGRef(jsc_context_get_value(context.get(), "b1"));
    checker.watch(result.get());
    g_assert_true(result.get() == value.get());

    value = adoptGRef(jsc_value_new_boolean(context.get(), FALSE));
    checker.watch(value.get());
    g_assert_true(jsc_value_is_boolean(value.get()));
    g_assert_true(jsc_value_to_boolean(value.get()) == FALSE);
    g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 0);
    valueString.reset(jsc_value_to_string(value.get()));
    g_assert_cmpstr(valueString.get(), ==, "false");
    jsc_context_set_value(context.get(), "b2", value.get());
    result = adoptGRef(jsc_context_get_value(context.get(), "b2"));
    checker.watch(result.get());
    g_assert_true(result.get() == value.get());

    value = adoptGRef(jsc_value_new_string(context.get(), "Hello World"));
    checker.watch(value.get());
    g_assert_true(jsc_value_is_string(value.get()));
    valueString.reset(jsc_value_to_string(value.get()));
    g_assert_cmpstr(valueString.get(), ==, "Hello World");
    g_assert_true(jsc_value_to_boolean(value.get()) == TRUE);
    g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 0);
    jsc_context_set_value(context.get(), "s1", value.get());
    result = adoptGRef(jsc_context_get_value(context.get(), "s1"));
    checker.watch(result.get());
    g_assert_true(result.get() == value.get());

    value = adoptGRef(jsc_value_new_string(context.get(), nullptr));
    checker.watch(value.get());
    g_assert_true(jsc_value_is_string(value.get()));
    valueString.reset(jsc_value_to_string(value.get()));
    g_assert_cmpstr(valueString.get(), ==, "");
    g_assert_true(jsc_value_to_boolean(value.get()) == FALSE);
    g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 0);
    jsc_context_set_value(context.get(), "s2", value.get());
    result = adoptGRef(jsc_context_get_value(context.get(), "s2"));
    checker.watch(result.get());
    g_assert_true(result.get() == value.get());

    value = adoptGRef(jsc_value_new_string(context.get(), "12.5"));
    checker.watch(value.get());
    g_assert_true(jsc_value_is_string(value.get()));
    valueString.reset(jsc_value_to_string(value.get()));
    g_assert_cmpstr(valueString.get(), ==, "12.5");
    g_assert_true(jsc_value_to_boolean(value.get()) == TRUE);
    g_assert_cmpfloat(jsc_value_to_double(value.get()), ==, 12.5);
    g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 12);
    jsc_context_set_value(context.get(), "s3", value.get());
    result = adoptGRef(jsc_context_get_value(context.get(), "s3"));
    checker.watch(result.get());
    g_assert_true(result.get() == value.get());

    value = adoptGRef(jsc_value_new_array(context.get(), G_TYPE_NONE));
    checker.watch(value.get());
    g_assert_true(jsc_value_is_array(value.get()));
    g_assert_true(jsc_value_is_object(value.get()));
    g_assert_true(jsc_value_to_boolean(value.get()) == TRUE);
    g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 0);
    valueString.reset(jsc_value_to_string(value.get()));
    g_assert_cmpstr(valueString.get(), ==, "");
    jsc_context_set_value(context.get(), "a1", value.get());
    result = adoptGRef(jsc_context_get_value(context.get(), "a1"));
    checker.watch(result.get());
    g_assert_true(result.get() == value.get());
    GRefPtr<JSCValue> arrayLength = adoptGRef(jsc_value_object_get_property(value.get(), "length"));
    checker.watch(arrayLength.get());
    g_assert_true(jsc_value_is_number(arrayLength.get()));
    g_assert_cmpint(jsc_value_to_int32(arrayLength.get()), ==, 0);
    GRefPtr<JSCValue> arrayItem = adoptGRef(jsc_value_new_number(context.get(), 1));
    checker.watch(arrayItem.get());
    jsc_value_object_set_property_at_index(value.get(), jsc_value_to_int32(arrayLength.get()), arrayItem.get());
    arrayLength = adoptGRef(jsc_value_object_get_property(value.get(), "length"));
    checker.watch(arrayLength.get());
    g_assert_true(jsc_value_is_number(arrayLength.get()));
    g_assert_cmpint(jsc_value_to_int32(arrayLength.get()), ==, 1);
    valueString.reset(jsc_value_to_string(value.get()));
    g_assert_cmpstr(valueString.get(), ==, "1");
    arrayItem = adoptGRef(jsc_value_new_number(context.get(), 5));
    checker.watch(arrayItem.get());
    jsc_value_object_set_property_at_index(value.get(), jsc_value_to_int32(arrayLength.get()), arrayItem.get());
    arrayLength = adoptGRef(jsc_value_object_get_property(value.get(), "length"));
    checker.watch(arrayLength.get());
    g_assert_true(jsc_value_is_number(arrayLength.get()));
    g_assert_cmpint(jsc_value_to_int32(arrayLength.get()), ==, 2);
    valueString.reset(jsc_value_to_string(value.get()));
    g_assert_cmpstr(valueString.get(), ==, "1,5");
    arrayItem = adoptGRef(jsc_value_new_number(context.get(), 10));
    checker.watch(arrayItem.get());
    jsc_value_object_set_property_at_index(value.get(), 3, arrayItem.get());
    arrayLength = adoptGRef(jsc_value_object_get_property(value.get(), "length"));
    checker.watch(arrayLength.get());
    g_assert_true(jsc_value_is_number(arrayLength.get()));
    g_assert_cmpint(jsc_value_to_int32(arrayLength.get()), ==, 4);
    arrayItem = adoptGRef(jsc_value_object_get_property_at_index(value.get(), 0));
    checker.watch(arrayItem.get());
    g_assert_true(jsc_value_is_number(arrayItem.get()));
    g_assert_cmpint(jsc_value_to_int32(arrayItem.get()), ==, 1);
    arrayItem = adoptGRef(jsc_value_object_get_property_at_index(value.get(), 1));
    checker.watch(arrayItem.get());
    g_assert_true(jsc_value_is_number(arrayItem.get()));
    g_assert_cmpint(jsc_value_to_int32(arrayItem.get()), ==, 5);
    arrayItem = adoptGRef(jsc_value_object_get_property_at_index(value.get(), 2));
    checker.watch(arrayItem.get());
    g_assert_true(jsc_value_is_undefined(arrayItem.get()));
    arrayItem = adoptGRef(jsc_value_object_get_property_at_index(value.get(), 3));
    checker.watch(arrayItem.get());
    g_assert_true(jsc_value_is_number(arrayItem.get()));
    g_assert_cmpint(jsc_value_to_int32(arrayItem.get()), ==, 10);
    arrayLength = adoptGRef(jsc_value_object_get_property(value.get(), "length"));
    checker.watch(arrayLength.get());
    arrayItem = adoptGRef(jsc_value_object_get_property_at_index(value.get(), jsc_value_to_int32(arrayLength.get()) + 1));
    checker.watch(arrayItem.get());
    g_assert_true(jsc_value_is_undefined(arrayItem.get()));

    GRefPtr<JSCValue> array = adoptGRef(jsc_value_new_array(context.get(), G_TYPE_INT, 1, G_TYPE_STRING, "two", G_TYPE_BOOLEAN, TRUE, JSC_TYPE_VALUE, value.get(), G_TYPE_NONE));
    checker.watch(array.get());
    g_assert_true(jsc_value_is_array(array.get()));
    g_assert_true(jsc_value_is_object(array.get()));
    g_assert_true(jsc_value_to_boolean(array.get()) == TRUE);
    g_assert_cmpint(jsc_value_to_int32(array.get()), ==, 0);
    valueString.reset(jsc_value_to_string(array.get()));
    g_assert_cmpstr(valueString.get(), ==, "1,two,true,1,5,,10");
    arrayLength = adoptGRef(jsc_value_object_get_property(array.get(), "length"));
    checker.watch(arrayLength.get());
    g_assert_true(jsc_value_is_number(arrayLength.get()));
    g_assert_cmpint(jsc_value_to_int32(arrayLength.get()), ==, 4);
    arrayItem = adoptGRef(jsc_value_object_get_property_at_index(array.get(), 0));
    checker.watch(arrayItem.get());
    g_assert_true(jsc_value_is_number(arrayItem.get()));
    g_assert_cmpint(jsc_value_to_int32(arrayItem.get()), ==, 1);
    arrayItem = adoptGRef(jsc_value_object_get_property_at_index(array.get(), 1));
    checker.watch(arrayItem.get());
    g_assert_true(jsc_value_is_string(arrayItem.get()));
    valueString.reset(jsc_value_to_string(arrayItem.get()));
    g_assert_cmpstr(valueString.get(), ==, "two");
    arrayItem = adoptGRef(jsc_value_object_get_property_at_index(array.get(), 2));
    checker.watch(arrayItem.get());
    g_assert_true(jsc_value_is_boolean(arrayItem.get()));
    g_assert_true(jsc_value_to_boolean(arrayItem.get()) == TRUE);
    arrayItem = adoptGRef(jsc_value_object_get_property_at_index(array.get(), 3));
    checker.watch(arrayItem.get());
    g_assert_true(arrayItem.get() == value.get());

    GRefPtr<GPtrArray> gArray = adoptGRef(g_ptr_array_new_with_free_func(g_object_unref));
    g_ptr_array_add(gArray.get(), jsc_value_new_number(context.get(), 1));
    g_ptr_array_add(gArray.get(), jsc_value_new_string(context.get(), "two"));
    g_ptr_array_add(gArray.get(), jsc_value_new_boolean(context.get(), TRUE));
    g_ptr_array_add(gArray.get(), g_object_ref(value.get()));
    array = adoptGRef(jsc_value_new_array_from_garray(context.get(), gArray.get()));
    checker.watch(array.get());
    g_assert_true(jsc_value_is_array(array.get()));
    g_assert_true(jsc_value_is_object(array.get()));
    g_assert_true(jsc_value_to_boolean(array.get()) == TRUE);
    g_assert_cmpint(jsc_value_to_int32(array.get()), ==, 0);
    valueString.reset(jsc_value_to_string(array.get()));
    g_assert_cmpstr(valueString.get(), ==, "1,two,true,1,5,,10");
    arrayLength = adoptGRef(jsc_value_object_get_property(array.get(), "length"));
    checker.watch(arrayLength.get());
    g_assert_true(jsc_value_is_number(arrayLength.get()));
    g_assert_cmpint(jsc_value_to_int32(arrayLength.get()), ==, gArray->len);

    value = adoptGRef(jsc_value_new_object(context.get(), nullptr, nullptr));
    checker.watch(value.get());
    g_assert_true(jsc_value_is_object(value.get()));
    g_assert_true(jsc_value_to_boolean(value.get()) == TRUE);
    g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 0);
    valueString.reset(jsc_value_to_string(value.get()));
    g_assert_cmpstr(valueString.get(), ==, "[object Object]");
    jsc_context_set_value(context.get(), "o", value.get());
    result = adoptGRef(jsc_context_get_value(context.get(), "o"));
    checker.watch(result.get());
    g_assert_true(result.get() == value.get());

    value = adoptGRef(jsc_value_new_undefined(context.get()));
    checker.watch(value.get());
    g_assert_true(jsc_value_is_undefined(value.get()));
    g_assert_true(jsc_value_to_boolean(value.get()) == FALSE);
    g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 0);
    valueString.reset(jsc_value_to_string(value.get()));
    g_assert_cmpstr(valueString.get(), ==, "undefined");
    jsc_context_set_value(context.get(), "u", value.get());
    result = adoptGRef(jsc_context_get_value(context.get(), "u"));
    checker.watch(result.get());
    g_assert_true(result.get() == value.get());

    value = adoptGRef(jsc_value_new_null(context.get()));
    checker.watch(value.get());
    g_assert_true(jsc_value_is_null(value.get()));
    g_assert_true(jsc_value_to_boolean(value.get()) == FALSE);
    g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 0);
    valueString.reset(jsc_value_to_string(value.get()));
    g_assert_cmpstr(valueString.get(), ==, "null");
    jsc_context_set_value(context.get(), "n", value.get());
    result = adoptGRef(jsc_context_get_value(context.get(), "n"));
    checker.watch(result.get());
    g_assert_true(result.get() == value.get());
}

static int foo(int n)
{
    return n * 2;
}

static void callback(JSCValue* function)
{
    GRefPtr<JSCValue> value = adoptGRef(jsc_value_function_call(function, G_TYPE_INT, 200, G_TYPE_NONE));
    g_assert_true(jsc_value_is_undefined(value.get()));
}

static void doubleAndSetInResult(int n)
{
    GRefPtr<JSCValue> value = adoptGRef(jsc_value_new_number(jsc_context_get_current(), n * 2));
    jsc_context_set_value(jsc_context_get_current(), "result", value.get());
}

static int sumFunction(GPtrArray* array)
{
    int retval = 0;

    g_ptr_array_foreach(array, [](gpointer data, gpointer userData) {
        g_assert_true(JSC_IS_VALUE(data));
        JSCValue* item = JSC_VALUE(data);
        g_assert_true(jsc_value_is_number(item));
        *static_cast<int*>(userData) += jsc_value_to_int32(item);
    }, &retval);

    return retval;
}

static void testJSCFunction()
{
    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        GRefPtr<JSCValue> function = adoptGRef(jsc_value_new_function(context.get(), "foo", G_CALLBACK(foo), nullptr, nullptr,  G_TYPE_INT, 1, G_TYPE_INT));
        checker.watch(function.get());
        jsc_context_set_value(context.get(), "foo", function.get());
        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "foo(200)", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpint(jsc_value_to_int32(result.get()), ==, 400);

        GRefPtr<JSCValue> value = adoptGRef(jsc_value_function_call(function.get(), G_TYPE_INT, 200, G_TYPE_NONE));
        checker.watch(value.get());
        g_assert_true(value.get() == result.get());

        GRefPtr<GPtrArray> parameters = adoptGRef(g_ptr_array_new_with_free_func(g_object_unref));
        auto* parameter = jsc_value_new_number(context.get(), 200);
        checker.watch(parameter);
        g_ptr_array_add(parameters.get(), parameter);
        value = adoptGRef(jsc_value_function_callv(function.get(), parameters->len, reinterpret_cast<JSCValue**>(parameters->pdata)));
        checker.watch(value.get());
        g_assert_true(value.get() == result.get());
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        GType parameterTypes[] = { G_TYPE_INT };
        GRefPtr<JSCValue> function = adoptGRef(jsc_value_new_functionv(context.get(), "foo", G_CALLBACK(foo), nullptr, nullptr,  G_TYPE_INT, 1, parameterTypes));
        checker.watch(function.get());
        jsc_context_set_value(context.get(), "foo", function.get());
        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "foo(200)", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpint(jsc_value_to_int32(result.get()), ==, 400);

        GRefPtr<JSCValue> value = adoptGRef(jsc_value_function_call(function.get(), G_TYPE_INT, 200, G_TYPE_NONE));
        checker.watch(value.get());
        g_assert_true(value.get() == result.get());

        GRefPtr<GPtrArray> parameters = adoptGRef(g_ptr_array_new_with_free_func(g_object_unref));
        auto* parameter = jsc_value_new_number(context.get(), 200);
        checker.watch(parameter);
        g_ptr_array_add(parameters.get(), parameter);
        value = adoptGRef(jsc_value_function_callv(function.get(), parameters->len, reinterpret_cast<JSCValue**>(parameters->pdata)));
        checker.watch(value.get());
        g_assert_true(value.get() == result.get());
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        GRefPtr<JSCValue> function = adoptGRef(jsc_context_evaluate(context.get(), "foo = function(n) { return n * 2; }", -1));
        checker.watch(function.get());

        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "foo(200)", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpint(jsc_value_to_int32(result.get()), ==, 400);

        GRefPtr<JSCValue> value = adoptGRef(jsc_value_function_call(function.get(), G_TYPE_INT, 200, G_TYPE_NONE));
        checker.watch(value.get());
        g_assert_true(value.get() == result.get());

        GRefPtr<GPtrArray> parameters = adoptGRef(g_ptr_array_new_with_free_func(g_object_unref));
        auto* parameter = jsc_value_new_number(context.get(), 200);
        checker.watch(parameter);
        g_ptr_array_add(parameters.get(), parameter);
        value = adoptGRef(jsc_value_function_callv(function.get(), parameters->len, reinterpret_cast<JSCValue**>(parameters->pdata)));
        checker.watch(value.get());
        g_assert_true(value.get() == result.get());
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        GRefPtr<JSCValue> function = adoptGRef(jsc_value_new_function(context.get(), "callback", G_CALLBACK(callback), nullptr, nullptr, G_TYPE_NONE, 1, JSC_TYPE_VALUE));
        checker.watch(function.get());
        jsc_context_set_value(context.get(), "callback", function.get());
        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "var result = 0; callback(function(n){ result = n * 2; }); result", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpint(jsc_value_to_int32(result.get()), ==, 400);

        result = adoptGRef(jsc_context_evaluate(context.get(), "result = 0", -1));
        checker.watch(result.get());
        result = adoptGRef(jsc_context_evaluate(context.get(), "result", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpint(jsc_value_to_int32(result.get()), ==, 0);

        GRefPtr<JSCValue> dbl = adoptGRef(jsc_value_new_function(context.get(), "doubleAndSetInResult", G_CALLBACK(doubleAndSetInResult), nullptr, nullptr, G_TYPE_NONE, 1, G_TYPE_INT));
        checker.watch(dbl.get());
        GRefPtr<JSCValue> value = adoptGRef(jsc_value_function_call(function.get(), JSC_TYPE_VALUE, dbl.get(), G_TYPE_NONE));
        checker.watch(value.get());
        result = adoptGRef(jsc_context_evaluate(context.get(), "result", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpint(jsc_value_to_int32(result.get()), ==, 400);

        GRefPtr<GPtrArray> parameters = adoptGRef(g_ptr_array_new());
        g_ptr_array_add(parameters.get(), dbl.get());
        value = adoptGRef(jsc_value_function_callv(function.get(), parameters->len, reinterpret_cast<JSCValue**>(parameters->pdata)));
        checker.watch(value.get());
        result = adoptGRef(jsc_context_evaluate(context.get(), "result", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpint(jsc_value_to_int32(result.get()), ==, 400);
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        GRefPtr<JSCValue> function = adoptGRef(jsc_context_evaluate(context.get(),
            "sumFunction = function(array) {\n"
            "    var result = 0;\n"
            "    for (var i in array) { result += array[i]; }\n"
            "    return result;\n"
            "}", -1));
        checker.watch(function.get());
        g_assert_true(jsc_value_is_object(function.get()));

        GRefPtr<JSCValue> array = adoptGRef(jsc_value_new_array(context.get(), G_TYPE_INT, 2, G_TYPE_INT, 4, G_TYPE_INT, 6, G_TYPE_NONE));
        checker.watch(array.get());

        GRefPtr<JSCValue> value = adoptGRef(jsc_value_function_call(function.get(), JSC_TYPE_VALUE, array.get(), G_TYPE_NONE));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 12);

        GRefPtr<GPtrArray> gArray = adoptGRef(g_ptr_array_new_with_free_func(g_object_unref));
        g_ptr_array_add(gArray.get(), jsc_value_new_number(context.get(), 1));
        g_ptr_array_add(gArray.get(), jsc_value_new_number(context.get(), 3));
        g_ptr_array_add(gArray.get(), jsc_value_new_number(context.get(), 5));

        value = adoptGRef(jsc_value_function_call(function.get(), G_TYPE_PTR_ARRAY, gArray.get(), G_TYPE_NONE));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 9);
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        GRefPtr<JSCValue> function = adoptGRef(jsc_value_new_function(context.get(), "sumFunction", G_CALLBACK(sumFunction), nullptr, nullptr, G_TYPE_INT, 1, G_TYPE_PTR_ARRAY));
        checker.watch(function.get());
        jsc_context_set_value(context.get(), "sumFunction", function.get());
        GRefPtr<JSCValue> value = adoptGRef(jsc_context_evaluate(context.get(), "sumFunction([2,4,6])", -1));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 12);

        GRefPtr<GPtrArray> gArray = adoptGRef(g_ptr_array_new_with_free_func(g_object_unref));
        g_ptr_array_add(gArray.get(), jsc_value_new_number(context.get(), 1));
        g_ptr_array_add(gArray.get(), jsc_value_new_number(context.get(), 3));
        g_ptr_array_add(gArray.get(), jsc_value_new_number(context.get(), 5));

        value = adoptGRef(jsc_value_function_call(function.get(), G_TYPE_PTR_ARRAY, gArray.get(), G_TYPE_NONE));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 9);
    }
}

static void testJSCObject()
{
    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        GRefPtr<JSCValue> foo = adoptGRef(jsc_context_evaluate(context.get(), "class Foo { foo(n) { return n * 2; } }; foo = new Foo;", -1));
        checker.watch(foo.get());
        g_assert_true(jsc_value_is_object(foo.get()));
        g_assert_true(jsc_value_object_is_instance_of(foo.get(), "Foo"));
        g_assert_true(jsc_value_object_has_property(foo.get(), "foo"));

        GUniquePtr<char*> properties(jsc_value_object_enumerate_properties(foo.get()));
        g_assert_null(properties.get());

        GRefPtr<JSCValue> result = adoptGRef(jsc_value_object_invoke_method(foo.get(), "foo", G_TYPE_INT, 200, G_TYPE_NONE));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpint(jsc_value_to_int32(result.get()), ==, 400);

        GRefPtr<GPtrArray> parameters = adoptGRef(g_ptr_array_new_with_free_func(g_object_unref));
        auto* parameter = jsc_value_new_number(context.get(), 200);
        checker.watch(parameter);
        g_ptr_array_add(parameters.get(), parameter);
        result = adoptGRef(jsc_value_object_invoke_methodv(foo.get(), "foo", parameters->len, reinterpret_cast<JSCValue**>(parameters->pdata)));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpint(jsc_value_to_int32(result.get()), ==, 400);

        g_assert_false(jsc_value_object_has_property(foo.get(), "bar"));
        bool didThrow = false;
        g_assert_throw_begin(exceptionHandler, didThrow);
        result = adoptGRef(jsc_value_object_invoke_method(foo.get(), "bar", G_TYPE_INT, 200, G_TYPE_NONE));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_undefined(result.get()));
        g_assert_did_throw(exceptionHandler, didThrow);

        g_assert_throw_begin(exceptionHandler, didThrow);
        result = adoptGRef(jsc_value_object_invoke_methodv(foo.get(), "bar", parameters->len, reinterpret_cast<JSCValue**>(parameters->pdata)));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_undefined(result.get()));
        g_assert_did_throw(exceptionHandler, didThrow);

        GRefPtr<JSCValue> constructor = adoptGRef(jsc_context_evaluate(context.get(), "Foo", -1));
        checker.watch(constructor.get());
        g_assert_true(jsc_value_is_constructor(constructor.get()));

        foo = adoptGRef(jsc_value_constructor_call(constructor.get(), G_TYPE_NONE));
        checker.watch(foo.get());
        g_assert_true(jsc_value_is_object(foo.get()));
        g_assert_true(jsc_value_object_is_instance_of(foo.get(), "Foo"));

        result = adoptGRef(jsc_value_object_invoke_method(foo.get(), "foo", G_TYPE_INT, 300, G_TYPE_NONE));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpint(jsc_value_to_int32(result.get()), ==, 600);

        jsc_context_set_value(context.get(), "foo2", foo.get());
        result = adoptGRef(jsc_context_evaluate(context.get(), "foo2 instanceof Foo", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));

        result = adoptGRef(jsc_context_evaluate(context.get(), "foo2.foo(500)", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpint(jsc_value_to_int32(result.get()), ==, 1000);

        GRefPtr<JSCValue> foo2 = adoptGRef(jsc_value_constructor_callv(constructor.get(), 0, nullptr));
        checker.watch(foo2.get());
        g_assert_true(jsc_value_is_object(foo2.get()));
        g_assert_true(jsc_value_object_is_instance_of(foo2.get(), "Foo"));
        g_assert_false(foo.get() == foo2.get());
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        GRefPtr<JSCValue> object = adoptGRef(jsc_value_new_object(context.get(), nullptr, nullptr));
        checker.watch(object.get());
        g_assert_true(jsc_value_is_object(object.get()));
        g_assert_true(jsc_value_object_is_instance_of(object.get(), "Object"));

        GUniquePtr<char*> properties(jsc_value_object_enumerate_properties(object.get()));
        g_assert_null(properties.get());

        GRefPtr<JSCValue> property = adoptGRef(jsc_value_new_number(context.get(), 25));
        checker.watch(property.get());
        g_assert_false(jsc_value_object_has_property(object.get(), "val"));
        jsc_value_object_define_property_data(object.get(), "val", static_cast<JSCValuePropertyFlags>(0), property.get());
        g_assert_true(jsc_value_object_has_property(object.get(), "val"));
        properties.reset(jsc_value_object_enumerate_properties(object.get()));
        g_assert_null(properties.get());
        jsc_context_set_value(context.get(), "f", object.get());

        GRefPtr<JSCValue> value = adoptGRef(jsc_context_evaluate(context.get(), "f.val;", -1));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 25);

        bool didThrow = false;
        g_assert_throw_begin(exceptionHandler, didThrow);
        value = adoptGRef(jsc_context_evaluate(context.get(), "'use strict'; f.val = 32;", -1));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_undefined(value.get()));
        g_assert_did_throw(exceptionHandler, didThrow);

        value = adoptGRef(jsc_context_evaluate(context.get(), "f.propertyIsEnumerable('val');", -1));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_boolean(value.get()));
        g_assert_true(jsc_value_to_boolean(value.get()) == FALSE);

        value = adoptGRef(jsc_context_evaluate(context.get(), "delete f.val;", -1));
        checker.watch(value.get());
        g_assert_true(jsc_value_object_has_property(object.get(), "val"));
        value = adoptGRef(jsc_context_evaluate(context.get(), "f.val;", -1));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 25);

        g_assert_false(jsc_value_object_delete_property(object.get(), "val"));
        g_assert_true(jsc_value_object_has_property(object.get(), "val"));

        property = adoptGRef(jsc_value_new_number(context.get(), 52));
        checker.watch(property.get());
        g_assert_throw_begin(exceptionHandler, didThrow);
        jsc_value_object_define_property_data(object.get(), "val", static_cast<JSCValuePropertyFlags>(0), property.get());
        g_assert_did_throw(exceptionHandler, didThrow);

        property = adoptGRef(jsc_value_new_number(context.get(), 32));
        checker.watch(property.get());
        g_assert_false(jsc_value_object_has_property(object.get(), "val2"));
        jsc_value_object_define_property_data(object.get(), "val2", static_cast<JSCValuePropertyFlags>(JSC_VALUE_PROPERTY_ENUMERABLE | JSC_VALUE_PROPERTY_WRITABLE), property.get());
        g_assert_true(jsc_value_object_has_property(object.get(), "val2"));
        value = adoptGRef(jsc_context_evaluate(context.get(), "f.val2;", -1));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 32);

        properties.reset(jsc_value_object_enumerate_properties(object.get()));
        g_assert_cmpuint(g_strv_length(properties.get()), ==, 1);
        g_assert_cmpstr(properties.get()[0], ==, "val2");
        g_assert_null(properties.get()[1]);

        value = adoptGRef(jsc_context_evaluate(context.get(), "'use strict'; f.val2 = 45;", -1));
        checker.watch(value.get());
        value = adoptGRef(jsc_context_evaluate(context.get(), "f.val2;", -1));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 45);

        value = adoptGRef(jsc_context_evaluate(context.get(), "f.propertyIsEnumerable('val2');", -1));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_boolean(value.get()));
        g_assert_true(jsc_value_to_boolean(value.get()) == TRUE);

        g_assert_false(jsc_value_object_delete_property(object.get(), "val2"));
        g_assert_true(jsc_value_object_has_property(object.get(), "val2"));

        property = adoptGRef(jsc_value_new_number(context.get(), 125));
        checker.watch(property.get());
        g_assert_false(jsc_value_object_has_property(object.get(), "val3"));
        jsc_value_object_define_property_data(object.get(), "val3", static_cast<JSCValuePropertyFlags>(JSC_VALUE_PROPERTY_CONFIGURABLE | JSC_VALUE_PROPERTY_WRITABLE), property.get());
        g_assert_true(jsc_value_object_has_property(object.get(), "val3"));
        value = adoptGRef(jsc_context_evaluate(context.get(), "f.val3;", -1));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 125);

        properties.reset(jsc_value_object_enumerate_properties(object.get()));
        g_assert_cmpuint(g_strv_length(properties.get()), ==, 1);
        g_assert_cmpstr(properties.get()[0], ==, "val2");
        g_assert_null(properties.get()[1]);

        property = adoptGRef(jsc_value_new_number(context.get(), 150));
        checker.watch(property.get());
        jsc_value_object_define_property_data(object.get(), "val3", static_cast<JSCValuePropertyFlags>(JSC_VALUE_PROPERTY_CONFIGURABLE), property.get());
        g_assert_true(jsc_value_object_has_property(object.get(), "val3"));
        value = adoptGRef(jsc_context_evaluate(context.get(), "f.val3;", -1));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 150);

        properties.reset(jsc_value_object_enumerate_properties(object.get()));
        g_assert_cmpuint(g_strv_length(properties.get()), ==, 1);
        g_assert_cmpstr(properties.get()[0], ==, "val2");
        g_assert_null(properties.get()[1]);

        value = adoptGRef(jsc_context_evaluate(context.get(), "delete f.val3;", -1));
        checker.watch(value.get());
        g_assert_false(jsc_value_object_has_property(object.get(), "val3"));
        value = adoptGRef(jsc_context_evaluate(context.get(), "f.val3;", -1));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_undefined(value.get()));

        property = adoptGRef(jsc_value_new_number(context.get(), 250));
        checker.watch(property.get());
        g_assert_false(jsc_value_object_has_property(object.get(), "val4"));
        jsc_value_object_define_property_data(object.get(), "val4", static_cast<JSCValuePropertyFlags>(JSC_VALUE_PROPERTY_CONFIGURABLE | JSC_VALUE_PROPERTY_ENUMERABLE), property.get());
        g_assert_true(jsc_value_object_has_property(object.get(), "val4"));
        value = adoptGRef(jsc_context_evaluate(context.get(), "f.val4;", -1));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 250);

        properties.reset(jsc_value_object_enumerate_properties(object.get()));
        g_assert_cmpuint(g_strv_length(properties.get()), ==, 2);
        g_assert_cmpstr(properties.get()[0], ==, "val2");
        g_assert_cmpstr(properties.get()[1], ==, "val4");
        g_assert_null(properties.get()[2]);

        g_assert_true(jsc_value_object_delete_property(object.get(), "val4"));
        g_assert_false(jsc_value_object_has_property(object.get(), "val4"));

        properties.reset(jsc_value_object_enumerate_properties(object.get()));
        g_assert_cmpuint(g_strv_length(properties.get()), ==, 1);
        g_assert_cmpstr(properties.get()[0], ==, "val2");
        g_assert_null(properties.get()[1]);

        GRefPtr<JSCValue> function = adoptGRef(jsc_value_new_function(context.get(), "foo", G_CALLBACK(foo), nullptr, nullptr, G_TYPE_INT, 1, G_TYPE_INT));
        checker.watch(function.get());

        g_assert_false(jsc_value_object_has_property(object.get(), "foo"));
        jsc_value_object_define_property_data(object.get(), "foo", static_cast<JSCValuePropertyFlags>(0), function.get());
        g_assert_true(jsc_value_object_has_property(object.get(), "foo"));

        properties.reset(jsc_value_object_enumerate_properties(object.get()));
        g_assert_cmpuint(g_strv_length(properties.get()), ==, 1);
        g_assert_cmpstr(properties.get()[0], ==, "val2");
        g_assert_null(properties.get()[1]);

        GRefPtr<JSCValue> result = adoptGRef(jsc_value_object_invoke_method(object.get(), "foo", G_TYPE_INT, 200, G_TYPE_NONE));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpint(jsc_value_to_int32(result.get()), ==, 400);

        g_assert_throw_begin(exceptionHandler, didThrow);
        result = adoptGRef(jsc_value_object_invoke_method(object.get(), "foo", G_TYPE_POINTER, "200", G_TYPE_NONE));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_undefined(result.get()));
        g_assert_did_throw(exceptionHandler, didThrow);
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        GRefPtr<JSCValue> value = adoptGRef(jsc_value_new_number(context.get(), 125));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_false(jsc_value_is_object(value.get()));
        g_assert_false(jsc_value_object_is_instance_of(value.get(), "Object"));

        bool didThrow = false;
        g_assert_throw_begin(exceptionHandler, didThrow);
        GRefPtr<JSCValue> result = adoptGRef(jsc_value_object_invoke_method(value.get(), "foo", G_TYPE_INT, 200, G_TYPE_NONE));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_undefined(result.get()));
        g_assert_throw_begin(exceptionHandler, didThrow);
    }
}

typedef struct _Foo Foo;
struct _Foo {
    int foo;
    HashMap<CString, int> properties;
    Foo* sibling;
};

static Foo* fooCreate()
{
    Foo* foo = g_new0(Foo, 1);
    new (foo) Foo();
    return foo;
}

static Foo* fooCreateWithFoo(int value)
{
    auto* f = fooCreate();
    f->foo = value;
    return f;
}

static void fooFree(Foo* foo)
{
    foo->~Foo();
    g_free(foo);
}

static void setFoo(Foo* foo, int value)
{
    foo->foo = value;
}

static int getFoo(Foo* foo)
{
    return foo->foo;
}

static void setSibling(Foo* foo, Foo* sibling)
{
    foo->sibling = sibling;
}

static Foo* getSibling(Foo* foo)
{
    return foo->sibling;
}

static void multiplyFoo(Foo* foo, int multiplier)
{
    foo->foo *= multiplier;
}

static int fooGetProperty(Foo* foo, const char* name)
{
    auto addResult = foo->properties.add(name, 0);
    return addResult.iterator->value;
}

static void fooSetProperty(Foo* foo, const char* name, int value)
{
    auto addResult = foo->properties.add(name, value);
    if (!addResult.isNewEntry)
        addResult.iterator->value = value;
}

struct PromiseData {
    Foo* foo;
    int multiplier;
    LeakChecker* checker;
};

static void getMultiplyFoo(JSCValue* resolve, JSCValue* reject, PromiseData* data)
{
    data->checker->watch(resolve);
    g_assert_true(JSC_IS_VALUE(resolve));
    g_assert_true(jsc_value_is_function(resolve));
    data->checker->watch(reject);
    g_assert_true(JSC_IS_VALUE(reject));
    g_assert_true(jsc_value_is_function(reject));

    GRefPtr<JSCValue> result;
    if (data->multiplier > 0)
        result = adoptGRef(jsc_value_function_call(resolve, G_TYPE_INT, data->foo->foo * data->multiplier, G_TYPE_NONE));
    else {
        GRefPtr<JSCException> exception = adoptGRef(jsc_exception_new(jsc_context_get_current(), "Multiplier must be positive greater than 0"));
        result = adoptGRef(jsc_value_function_call(reject, JSC_TYPE_EXCEPTION, exception.get(), G_TYPE_NONE));
    }
    data->checker->watch(result.get());
    g_assert_true(jsc_value_is_undefined(result.get()));
}

static JSCValue* getMultiplyFooAsync(Foo* foo, int multiplier, LeakChecker* checker)
{
    auto* jsContext = jsc_context_get_current();
    g_assert_true(JSC_IS_CONTEXT(jsContext));

    GRefPtr<JSCValue> function = adoptGRef(jsc_value_new_function(jsContext, nullptr, G_CALLBACK(getMultiplyFoo), new PromiseData { foo, multiplier, checker},
        [](gpointer data) { delete static_cast<PromiseData*>(data); }, G_TYPE_NONE, 2, JSC_TYPE_VALUE, JSC_TYPE_VALUE));
    checker->watch(function.get());
    GRefPtr<JSCValue> promise = adoptGRef(jsc_context_get_value(jsContext, "Promise"));
    checker->watch(promise.get());
    g_assert_true(jsc_value_is_constructor(promise.get()));
    auto* returnValue = jsc_value_constructor_call(promise.get(), JSC_TYPE_VALUE, function.get(), G_TYPE_NONE);
    checker->watch(returnValue);
    return returnValue;
}

typedef struct {
    int baz;
} Baz;

static Baz* bazCreate()
{
    return g_new0(Baz, 1);
}

static JSCClassVTable fooVTable = {
    // get_property
    [](JSCClass* jscClass, JSCContext* context, gpointer instance, const char* name) -> JSCValue* {
        auto* checker = static_cast<LeakChecker*>(g_object_get_data(G_OBJECT(jscClass), "leak-checker"));
        checker->watch(context);

        if (!g_str_has_prefix(name, "prop_"))
            return nullptr;

        if (!g_strcmp0(name, "prop_throw_on_get")) {
            jsc_context_throw(context, "Invalid property");
            return jsc_value_new_undefined(context);
        }

        auto* foo = static_cast<Foo*>(instance);
        auto* returnValue = jsc_value_new_number(context, fooGetProperty(foo, name));
        checker->watch(returnValue);
        return returnValue;
    },
    // set_property
    [](JSCClass* jscClass, JSCContext* context, gpointer instance, const char* name, JSCValue* value) -> gboolean {
        auto* checker = static_cast<LeakChecker*>(g_object_get_data(G_OBJECT(jscClass), "leak-checker"));
        checker->watch(context);
        checker->watch(value);

        if (!g_str_has_prefix(name, "prop_"))
            return FALSE;

        if (!jsc_value_is_number(value)) {
            jsc_context_throw(context, "Invalid value set: only numbers are allowed");
            return TRUE;
        }

        auto* foo = static_cast<Foo*>(instance);
        fooSetProperty(foo, name, jsc_value_to_int32(value));
        return true;
    },
    // has_property
    [](JSCClass* jscClass, JSCContext* context, gpointer instance, const char* name) -> gboolean {
        auto* checker = static_cast<LeakChecker*>(g_object_get_data(G_OBJECT(jscClass), "leak-checker"));
        checker->watch(context);
        return g_str_has_prefix(name, "prop_");
    },
    // delete_property
    [](JSCClass* jscClass, JSCContext* context, gpointer instance, const char* name) -> gboolean {
        auto* checker = static_cast<LeakChecker*>(g_object_get_data(G_OBJECT(jscClass), "leak-checker"));
        checker->watch(context);

        if (!g_strcmp0(name, "prop_cant_delete"))
            return FALSE;

        if (!g_strcmp0(name, "prop_throw_on_delete")) {
            jsc_context_throw(context, "Invalid property");
            return TRUE;
        }

        auto* foo = static_cast<Foo*>(instance);
        if (!foo->properties.contains(name))
            return FALSE;

        foo->properties.remove(name);
        return TRUE;
    },
    // enumerate_properties
    [](JSCClass* jscClass, JSCContext* context, gpointer instance) -> char** {
        auto* checker = static_cast<LeakChecker*>(g_object_get_data(G_OBJECT(jscClass), "leak-checker"));
        checker->watch(context);

        auto* foo = static_cast<Foo*>(instance);
        GRefPtr<GPtrArray> properties = adoptGRef(g_ptr_array_new_with_free_func(g_free));
        Vector<CString> names = copyToVector(foo->properties.keys());
        std::sort(names.begin(), names.end());
        for (const auto& name : names) {
            if (g_str_has_prefix(name.data(), "prop_enum_"))
                g_ptr_array_add(properties.get(), g_strdup(name.data()));
        }
        if (!properties->len)
            return nullptr;

        g_ptr_array_add(properties.get(), nullptr);
        return reinterpret_cast<char**>(g_ptr_array_free(properties.leakRef(), FALSE));
    },
    // padding
    nullptr, nullptr, nullptr, nullptr
};

static void testJSCClass()
{
    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        JSCClass* jscClass = jsc_context_register_class(context.get(), "Foo", nullptr, nullptr, reinterpret_cast<GDestroyNotify>(fooFree));
        checker.watch(jscClass);
        g_assert_false(jsc_class_get_parent(jscClass));

        GRefPtr<JSCValue> constructor = adoptGRef(jsc_class_add_constructor(jscClass, nullptr, G_CALLBACK(fooCreate), nullptr, nullptr, G_TYPE_POINTER, 0, G_TYPE_NONE));
        checker.watch(constructor.get());
        g_assert_true(jsc_value_is_constructor(constructor.get()));
        jsc_context_set_value(context.get(), jsc_class_get_name(jscClass), constructor.get());

        GRefPtr<JSCValue> foo = adoptGRef(jsc_context_evaluate(context.get(), "f = new Foo();", -1));
        checker.watch(foo.get());
        g_assert_true(jsc_value_is_object(foo.get()));
        g_assert_true(jsc_value_object_is_instance_of(foo.get(), jsc_class_get_name(jscClass)));
        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "f instanceof Foo;", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));

        GUniquePtr<char*> properties(jsc_value_object_enumerate_properties(foo.get()));
        g_assert_null(properties.get());

        g_assert_false(jsc_value_object_has_property(foo.get(), "getFoo"));
        jsc_class_add_method(jscClass, "getFoo", G_CALLBACK(getFoo), nullptr, nullptr, G_TYPE_INT, 0, G_TYPE_NONE);
        g_assert_true(jsc_value_object_has_property(foo.get(), "getFoo"));
        properties.reset(jsc_value_object_enumerate_properties(foo.get()));
        g_assert_null(properties.get());
        GRefPtr<JSCValue> value = adoptGRef(jsc_value_object_invoke_method(foo.get(), "getFoo", G_TYPE_NONE));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 0);

        value = adoptGRef(jsc_value_object_invoke_methodv(foo.get(), "getFoo", 0, nullptr));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 0);

        GRefPtr<JSCValue> value2 = adoptGRef(jsc_context_evaluate(context.get(), "f.getFoo()", -1));
        checker.watch(value2.get());
        g_assert_true(value.get() == value2.get());

        g_assert_false(jsc_value_object_has_property(foo.get(), "setFoo"));
        GType parameterTypes[] = { G_TYPE_INT };
        jsc_class_add_methodv(jscClass, "setFoo", G_CALLBACK(setFoo), nullptr, nullptr, G_TYPE_NONE, 1, parameterTypes);
        g_assert_true(jsc_value_object_has_property(foo.get(), "setFoo"));
        properties.reset(jsc_value_object_enumerate_properties(foo.get()));
        g_assert_null(properties.get());
        result = adoptGRef(jsc_value_object_invoke_method(foo.get(), "setFoo", G_TYPE_INT, 25, G_TYPE_NONE));
        checker.watch(result.get());
        value = adoptGRef(jsc_context_evaluate(context.get(), "f.getFoo()", -1));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 25);

        result = adoptGRef(jsc_context_evaluate(context.get(), "f.setFoo(45)", -1));
        checker.watch(result.get());
        value = adoptGRef(jsc_value_object_invoke_method(foo.get(), "getFoo", G_TYPE_NONE));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 45);

        value = adoptGRef(jsc_value_object_invoke_methodv(foo.get(), "getFoo", 0, nullptr));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 45);

        GRefPtr<JSCValue> constructor2 = adoptGRef(jsc_class_add_constructorv(jscClass, "CreateWithFoo", G_CALLBACK(fooCreateWithFoo), nullptr, nullptr, G_TYPE_POINTER, 1, parameterTypes));
        checker.watch(constructor2.get());
        g_assert_true(jsc_value_is_constructor(constructor2.get()));
        jsc_value_object_set_property(constructor.get(), "CreateWithFoo", constructor2.get());

        GRefPtr<JSCValue> foo2 = adoptGRef(jsc_context_evaluate(context.get(), "f2 = new Foo.CreateWithFoo(42);", -1));
        checker.watch(foo2.get());
        g_assert_true(jsc_value_is_object(foo2.get()));
        g_assert_true(jsc_value_object_is_instance_of(foo2.get(), jsc_class_get_name(jscClass)));
        g_assert_false(foo.get() == foo2.get());
        result = adoptGRef(jsc_context_evaluate(context.get(), "f2 instanceof Foo;", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));
        result = adoptGRef(jsc_context_evaluate(context.get(), "f2 instanceof Foo.CreateWithFoo;", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));

        g_assert_false(jsc_value_object_has_property(foo.get(), "foo"));
        g_assert_false(jsc_value_object_has_property(foo2.get(), "foo"));
        jsc_class_add_property(jscClass, "foo", G_TYPE_INT, G_CALLBACK(getFoo), G_CALLBACK(setFoo), nullptr, nullptr);
        g_assert_true(jsc_value_object_has_property(foo.get(), "foo"));
        g_assert_true(jsc_value_object_has_property(foo2.get(), "foo"));
        properties.reset(jsc_value_object_enumerate_properties(foo.get()));
        g_assert_null(properties.get());
        value = adoptGRef(jsc_context_evaluate(context.get(), "f2.foo", -1));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 42);
        result = adoptGRef(jsc_context_evaluate(context.get(), "f2.foo = 52", -1));
        checker.watch(result.get());
        value = adoptGRef(jsc_context_evaluate(context.get(), "f2.foo", -1));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 52);

        JSCClass* otherClass = jsc_context_register_class(context.get(), "Baz", nullptr, nullptr, g_free);
        checker.watch(otherClass);
        g_assert_false(jsc_class_get_parent(otherClass));

        GRefPtr<JSCValue> constructor3 = adoptGRef(jsc_class_add_constructor(otherClass, nullptr, G_CALLBACK(bazCreate), nullptr, nullptr, G_TYPE_POINTER, 0, G_TYPE_NONE));
        checker.watch(constructor3.get());
        g_assert_true(jsc_value_is_constructor(constructor3.get()));
        jsc_context_set_value(context.get(), jsc_class_get_name(otherClass), constructor3.get());

        g_assert_false(jsc_value_object_is_instance_of(foo.get(), jsc_class_get_name(otherClass)));
        g_assert_false(jsc_value_object_is_instance_of(foo2.get(), jsc_class_get_name(otherClass)));

        GRefPtr<JSCValue> baz = adoptGRef(jsc_value_constructor_call(constructor3.get(), G_TYPE_NONE));
        checker.watch(baz.get());
        g_assert_true(jsc_value_is_object(baz.get()));
        g_assert_true(jsc_value_object_is_instance_of(baz.get(), jsc_class_get_name(otherClass)));
        g_assert_false(jsc_value_object_is_instance_of(baz.get(), jsc_class_get_name(jscClass)));
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        JSCClass* jscClass = jsc_context_register_class(context.get(), "Foo", nullptr, nullptr, reinterpret_cast<GDestroyNotify>(fooFree));
        checker.watch(jscClass);

        GRefPtr<JSCValue> constructor = adoptGRef(jsc_class_add_constructor(jscClass, nullptr, G_CALLBACK(fooCreate), nullptr, nullptr, G_TYPE_POINTER, 0, G_TYPE_NONE));
        checker.watch(constructor.get());
        g_assert_true(jsc_value_is_constructor(constructor.get()));
        jsc_context_set_value(context.get(), jsc_class_get_name(jscClass), constructor.get());
        jsc_class_add_property(jscClass, "sibling", G_TYPE_POINTER, G_CALLBACK(getSibling), G_CALLBACK(setSibling), nullptr, nullptr);

        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "f1 = new Foo(); f2 = new Foo(); f2.sibling = f1", -1));
        checker.watch(result.get());

        GRefPtr<JSCValue> f1 = adoptGRef(jsc_context_get_value(context.get(), "f1"));
        checker.watch(f1.get());
        g_assert_true(jsc_value_is_object(f1.get()));
        g_assert_true(jsc_value_object_has_property(f1.get(), "sibling"));
        GUniquePtr<char*> properties(jsc_value_object_enumerate_properties(f1.get()));
        g_assert_null(properties.get());
        GRefPtr<JSCValue> f2 = adoptGRef(jsc_context_get_value(context.get(), "f2"));
        checker.watch(f2.get());
        g_assert_true(jsc_value_is_object(f2.get()));
        g_assert_true(jsc_value_object_has_property(f2.get(), "sibling"));
        properties.reset(jsc_value_object_enumerate_properties(f2.get()));
        g_assert_null(properties.get());

        GRefPtr<JSCValue> value = adoptGRef(jsc_context_evaluate(context.get(), "f2.sibling", -1));
        checker.watch(value.get());
        g_assert_true(value.get() == f1.get());

        jsc_value_object_set_property(f1.get(), "sibling", f2.get());
        value = adoptGRef(jsc_context_evaluate(context.get(), "f1.sibling", -1));
        checker.watch(value.get());
        g_assert_true(value.get() == f2.get());
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        JSCClass* jscClass = jsc_context_register_class(context.get(), "Foo", nullptr, nullptr, reinterpret_cast<GDestroyNotify>(fooFree));
        checker.watch(jscClass);

        GRefPtr<JSCValue> constructor = adoptGRef(jsc_class_add_constructorv(jscClass, nullptr, G_CALLBACK(fooCreate), nullptr, nullptr, G_TYPE_POINTER, 0, nullptr));
        checker.watch(constructor.get());
        g_assert_true(jsc_value_is_constructor(constructor.get()));
        jsc_context_set_value(context.get(), jsc_class_get_name(jscClass), constructor.get());
        jsc_class_add_property(jscClass, "foo", G_TYPE_INT, G_CALLBACK(getFoo), G_CALLBACK(setFoo), nullptr, nullptr);

        Foo* f = fooCreateWithFoo(25);
        GRefPtr<JSCValue> foo = adoptGRef(jsc_value_new_object(context.get(), f, jscClass));
        checker.watch(foo.get());
        g_assert_true(jsc_value_is_object(foo.get()));
        g_assert_true(jsc_value_object_is_instance_of(foo.get(), jsc_class_get_name(jscClass)));
        g_assert_true(jsc_value_object_has_property(foo.get(), "foo"));
        GUniquePtr<char*> properties(jsc_value_object_enumerate_properties(foo.get()));
        g_assert_null(properties.get());

        jsc_context_set_value(context.get(), "f", foo.get());
        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "f instanceof Foo;", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));

        result = adoptGRef(jsc_context_evaluate(context.get(), "f.foo", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpint(jsc_value_to_int32(result.get()), ==, 25);

        setFoo(f, 42);
        result = adoptGRef(jsc_context_evaluate(context.get(), "f.foo", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpint(jsc_value_to_int32(result.get()), ==, 42);

        JSCClass* bazClass = jsc_context_register_class(context.get(), "Baz", nullptr, nullptr, g_free);
        checker.watch(bazClass);

        GRefPtr<JSCValue> constructor2 = adoptGRef(jsc_class_add_constructor(bazClass, nullptr, G_CALLBACK(bazCreate), nullptr, nullptr, G_TYPE_POINTER, 0, G_TYPE_NONE));
        checker.watch(constructor2.get());
        g_assert_true(jsc_value_is_constructor(constructor2.get()));
        jsc_context_set_value(context.get(), jsc_class_get_name(bazClass), constructor2.get());

        Baz* bz = bazCreate();
        GRefPtr<JSCValue> baz = adoptGRef(jsc_value_new_object(context.get(), bz, bazClass));
        checker.watch(baz.get());
        g_assert_true(jsc_value_is_object(baz.get()));
        g_assert_true(jsc_value_object_is_instance_of(baz.get(), jsc_class_get_name(bazClass)));
        g_assert_false(jsc_value_object_is_instance_of(baz.get(), jsc_class_get_name(jscClass)));
        g_assert_false(jsc_value_object_is_instance_of(foo.get(), jsc_class_get_name(bazClass)));
        g_assert_false(jsc_value_object_has_property(baz.get(), "foo"));

        jsc_context_set_value(context.get(), "bz", baz.get());
        result = adoptGRef(jsc_context_evaluate(context.get(), "bz instanceof Baz;", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));

        result = adoptGRef(jsc_context_evaluate(context.get(), "bz instanceof Foo;", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_false(jsc_value_to_boolean(result.get()));

        result = adoptGRef(jsc_context_evaluate(context.get(), "f instanceof Baz;", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_false(jsc_value_to_boolean(result.get()));

        jsc_value_object_set_property(baz.get(), "foo", foo.get());
        result = adoptGRef(jsc_context_evaluate(context.get(), "bz.foo", -1));
        checker.watch(result.get());
        g_assert_true(result.get() == foo.get());
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        JSCClass* jscClass = jsc_context_register_class(context.get(), "Foo", nullptr, nullptr, reinterpret_cast<GDestroyNotify>(fooFree));
        checker.watch(jscClass);

        GRefPtr<JSCValue> constructor = adoptGRef(jsc_class_add_constructor(jscClass, nullptr, G_CALLBACK(fooCreate), nullptr, nullptr, G_TYPE_POINTER, 0, G_TYPE_NONE));
        checker.watch(constructor.get());
        g_assert_true(jsc_value_is_constructor(constructor.get()));
        jsc_context_set_value(context.get(), jsc_class_get_name(jscClass), constructor.get());
        jsc_class_add_property(jscClass, "foo", G_TYPE_INT, G_CALLBACK(getFoo), G_CALLBACK(setFoo), nullptr, nullptr);

        Foo* f = fooCreateWithFoo(25);
        GRefPtr<JSCValue> foo = adoptGRef(jsc_value_new_object(context.get(), f, jscClass));
        checker.watch(foo.get());
        g_assert_true(jsc_value_is_object(foo.get()));
        g_assert_true(jsc_value_object_is_instance_of(foo.get(), jsc_class_get_name(jscClass)));
        g_assert_true(jsc_value_object_has_property(foo.get(), "foo"));
        GUniquePtr<char*> properties(jsc_value_object_enumerate_properties(foo.get()));
        g_assert_null(properties.get());

        jsc_context_set_value(context.get(), "f1", foo.get());
        jsc_context_set_value(context.get(), "f2", foo.get());
        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "f1 === f2;", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));

        GRefPtr<JSCValue> property = adoptGRef(jsc_value_new_number(context.get(), 50));
        checker.watch(property.get());
        g_assert_false(jsc_value_object_has_property(foo.get(), "n"));
        jsc_value_object_set_property(foo.get(), "n", property.get());
        g_assert_true(jsc_value_object_has_property(foo.get(), "n"));
        result = adoptGRef(jsc_context_evaluate(context.get(), "f1.n", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpint(jsc_value_to_int32(result.get()), ==, 50);
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        JSCClass* jscClass = jsc_context_register_class(context.get(), "Foo", nullptr, nullptr, reinterpret_cast<GDestroyNotify>(fooFree));
        checker.watch(jscClass);
        g_assert_false(jsc_class_get_parent(jscClass));

        GRefPtr<JSCValue> constructor = adoptGRef(jsc_class_add_constructor(jscClass, nullptr, G_CALLBACK(fooCreate), nullptr, nullptr, G_TYPE_POINTER, 0, G_TYPE_NONE));
        checker.watch(constructor.get());
        g_assert_true(jsc_value_is_constructor(constructor.get()));
        jsc_context_set_value(context.get(), jsc_class_get_name(jscClass), constructor.get());
        jsc_class_add_property(jscClass, "foo", G_TYPE_INT, G_CALLBACK(getFoo), G_CALLBACK(setFoo), nullptr, nullptr);
        GRefPtr<JSCValue> foo = adoptGRef(jsc_context_evaluate(context.get(), "f = new Foo();", -1));
        checker.watch(foo.get());
        g_assert_true(jsc_value_is_object(foo.get()));
        g_assert_true(jsc_value_object_is_instance_of(foo.get(), jsc_class_get_name(jscClass)));
        g_assert_true(jsc_value_object_has_property(foo.get(), "foo"));
        GUniquePtr<char*> properties(jsc_value_object_enumerate_properties(foo.get()));
        g_assert_null(properties.get());

        GRefPtr<JSCValue> value = adoptGRef(jsc_value_new_number(context.get(), 125));
        checker.watch(value.get());
        jsc_value_object_set_property(foo.get(), "foo", value.get());

        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "f.__lookupGetter__('foo').call(f)", -1));
        checker.watch(result.get());
        g_assert_true(value.get() == result.get());

        bool didThrow = false;
        g_assert_throw_begin(exceptionHandler, didThrow);
        result = adoptGRef(jsc_context_evaluate(context.get(), "f.__lookupGetter__('foo').call({})", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_undefined(result.get()));
        g_assert_did_throw(exceptionHandler, didThrow);
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        JSCClass* jscClass = jsc_context_register_class(context.get(), "Foo", nullptr, nullptr, reinterpret_cast<GDestroyNotify>(fooFree));
        checker.watch(jscClass);
        g_assert_false(jsc_class_get_parent(jscClass));

        GRefPtr<JSCValue> constructor = adoptGRef(jsc_class_add_constructor(jscClass, nullptr, G_CALLBACK(fooCreate), nullptr, nullptr, G_TYPE_NONE, 0, G_TYPE_NONE));
        checker.watch(constructor.get());
        g_assert_true(jsc_value_is_constructor(constructor.get()));
        jsc_context_set_value(context.get(), jsc_class_get_name(jscClass), constructor.get());
        bool didThrow = false;
        g_assert_throw_begin(exceptionHandler, didThrow);
        GRefPtr<JSCValue> foo = adoptGRef(jsc_context_evaluate(context.get(), "f = new Foo();", -1));
        checker.watch(foo.get());
        g_assert_true(jsc_value_is_undefined(foo.get()));
        g_assert_did_throw(exceptionHandler, didThrow);
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        GRefPtr<JSCValue> jsNamespace = adoptGRef(jsc_value_new_object(context.get(), nullptr, nullptr));
        checker.watch(jsNamespace.get());
        g_assert_true(jsc_value_is_object(jsNamespace.get()));

        jsc_context_set_value(context.get(), "wk", jsNamespace.get());

        JSCClass* jscClass = jsc_context_register_class(context.get(), "Foo", nullptr, nullptr, reinterpret_cast<GDestroyNotify>(fooFree));
        checker.watch(jscClass);

        GRefPtr<JSCValue> constructor = adoptGRef(jsc_class_add_constructor(jscClass, nullptr, G_CALLBACK(fooCreate), nullptr, nullptr, G_TYPE_POINTER, 0, G_TYPE_NONE));
        checker.watch(constructor.get());
        g_assert_true(jsc_value_is_constructor(constructor.get()));
        jsc_value_object_set_property(jsNamespace.get(), jsc_class_get_name(jscClass), constructor.get());
        jsc_class_add_property(jscClass, "foo", G_TYPE_INT, G_CALLBACK(getFoo), G_CALLBACK(setFoo), nullptr, nullptr);

        bool didThrow = false;
        g_assert_throw_begin(exceptionHandler, didThrow);
        GRefPtr<JSCValue> foo = adoptGRef(jsc_context_evaluate(context.get(), "f = new Foo();", -1));
        checker.watch(foo.get());
        g_assert_true(jsc_value_is_undefined(foo.get()));
        g_assert_did_throw(exceptionHandler, didThrow);

        foo = adoptGRef(jsc_context_evaluate(context.get(), "f = new wk.Foo();", -1));
        g_assert_true(jsc_value_is_object(foo.get()));
        g_assert_true(jsc_value_object_is_instance_of(foo.get(), "wk.Foo"));
        g_assert_true(jsc_value_object_has_property(foo.get(), "foo"));
        GUniquePtr<char*> properties(jsc_value_object_enumerate_properties(foo.get()));
        g_assert_null(properties.get());
        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "f instanceof wk.Foo;", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));

        GRefPtr<JSCValue> constructor2 = adoptGRef(jsc_class_add_constructor(jscClass, "CreateWithFoo", G_CALLBACK(fooCreateWithFoo), nullptr, nullptr, G_TYPE_POINTER, 1, G_TYPE_INT));
        checker.watch(constructor2.get());
        g_assert_true(jsc_value_is_constructor(constructor2.get()));
        jsc_value_object_set_property(constructor.get(), "CreateWithFoo", constructor2.get());

        g_assert_throw_begin(exceptionHandler, didThrow);
        GRefPtr<JSCValue> foo2 = adoptGRef(jsc_context_evaluate(context.get(), "f2 = new Foo.CreateWithFoo(42);", -1));
        checker.watch(foo2.get());
        g_assert_true(jsc_value_is_undefined(foo2.get()));
        g_assert_did_throw(exceptionHandler, didThrow);

        foo2 = adoptGRef(jsc_context_evaluate(context.get(), "f2 = new wk.Foo.CreateWithFoo(42);", -1));
        checker.watch(foo2.get());
        g_assert_true(jsc_value_is_object(foo2.get()));
        g_assert_true(jsc_value_object_is_instance_of(foo2.get(), "wk.Foo"));
        g_assert_true(jsc_value_object_is_instance_of(foo2.get(), "wk.Foo.CreateWithFoo"));
        g_assert_false(foo.get() == foo2.get());
        result = adoptGRef(jsc_context_evaluate(context.get(), "f2 instanceof wk.Foo;", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));
        result = adoptGRef(jsc_context_evaluate(context.get(), "f2 instanceof wk.Foo.CreateWithFoo;", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));
        result = adoptGRef(jsc_context_evaluate(context.get(), "f2.foo", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpint(jsc_value_to_int32(result.get()), ==, 42);

        GRefPtr<GPtrArray> parameters = adoptGRef(g_ptr_array_new_with_free_func(g_object_unref));
        auto* parameter = jsc_value_new_number(context.get(), 62);
        checker.watch(parameter);
        g_ptr_array_add(parameters.get(), parameter);

        GRefPtr<JSCValue> foo3 = adoptGRef(jsc_value_constructor_callv(constructor2.get(), parameters->len, reinterpret_cast<JSCValue**>(parameters->pdata)));
        checker.watch(foo3.get());
        g_assert_true(jsc_value_is_object(foo3.get()));
        g_assert_true(jsc_value_object_is_instance_of(foo3.get(), "wk.Foo"));
        g_assert_true(jsc_value_object_is_instance_of(foo3.get(), "wk.Foo.CreateWithFoo"));
        g_assert_false(foo2.get() == foo3.get());
        jsc_context_set_value(context.get(), "f3", foo3.get());
        result = adoptGRef(jsc_context_evaluate(context.get(), "f3 instanceof wk.Foo;", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));
        result = adoptGRef(jsc_context_evaluate(context.get(), "f3 instanceof wk.Foo.CreateWithFoo;", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));
        result = adoptGRef(jsc_context_evaluate(context.get(), "f3.foo", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpint(jsc_value_to_int32(result.get()), ==, 62);
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        JSCClass* jscClass = jsc_context_register_class(context.get(), "Foo", nullptr, &fooVTable, reinterpret_cast<GDestroyNotify>(fooFree));
        checker.watch(jscClass);
        g_object_set_data(G_OBJECT(jscClass), "leak-checker", &checker);

        GRefPtr<JSCValue> constructor = adoptGRef(jsc_class_add_constructor(jscClass, nullptr, G_CALLBACK(fooCreate), nullptr, nullptr, G_TYPE_POINTER, 0, G_TYPE_NONE));
        checker.watch(constructor.get());
        g_assert_true(jsc_value_is_constructor(constructor.get()));
        jsc_context_set_value(context.get(), jsc_class_get_name(jscClass), constructor.get());
        jsc_class_add_property(jscClass, "foo", G_TYPE_INT, G_CALLBACK(getFoo), G_CALLBACK(setFoo), nullptr, nullptr);

        GRefPtr<JSCValue> foo = adoptGRef(jsc_context_evaluate(context.get(), "f = new Foo();", -1));
        checker.watch(foo.get());
        g_assert_true(jsc_value_is_object(foo.get()));
        g_assert_true(jsc_value_object_has_property(foo.get(), "foo"));

        g_assert_true(jsc_value_object_has_property(foo.get(), "prop_whatever"));
        g_assert_false(jsc_value_object_has_property(foo.get(), "whatever_prop"));

        GUniquePtr<char*> properties(jsc_value_object_enumerate_properties(foo.get()));
        g_assert_null(properties.get());

        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "f.prop_1", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpuint(jsc_value_to_int32(result.get()), ==, 0);

        GRefPtr<JSCValue> value = adoptGRef(jsc_value_object_get_property(foo.get(), "prop_1"));
        checker.watch(value.get());
        g_assert_true(value.get() == result.get());

        result = adoptGRef(jsc_context_evaluate(context.get(), "f.foo", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpuint(jsc_value_to_int32(result.get()), ==, 0);

        value = adoptGRef(jsc_value_object_get_property(foo.get(), "foo"));
        checker.watch(value.get());
        g_assert_true(value.get() == result.get());

        result = adoptGRef(jsc_context_evaluate(context.get(), "'foo' in f.__proto__", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));

        result = adoptGRef(jsc_context_evaluate(context.get(), "'foo' in f", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));

        result = adoptGRef(jsc_context_evaluate(context.get(), "'prop_1' in f.__proto__", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_false(jsc_value_to_boolean(result.get()));

        result = adoptGRef(jsc_context_evaluate(context.get(), "'prop_1' in f", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));

        result = adoptGRef(jsc_context_evaluate(context.get(), "f.prop_1 = 25", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpuint(jsc_value_to_int32(result.get()), ==, 25);

        g_assert_true(jsc_value_object_delete_property(foo.get(), "prop_1"));
        g_assert_true(jsc_value_object_has_property(foo.get(), "prop_1"));
        value = adoptGRef(jsc_value_object_get_property(foo.get(), "prop_1"));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpuint(jsc_value_to_int32(value.get()), ==, 0);

        result = adoptGRef(jsc_context_evaluate(context.get(), "f.prop_cant_delete = 125", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpuint(jsc_value_to_int32(result.get()), ==, 125);
        jsc_value_object_delete_property(foo.get(), "prop_cant_delete");
        g_assert_true(jsc_value_object_has_property(foo.get(), "prop_cant_delete"));
        value = adoptGRef(jsc_value_object_get_property(foo.get(), "prop_cant_delete"));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpuint(jsc_value_to_int32(value.get()), ==, 125);

        value = adoptGRef(jsc_value_new_number(context.get(), 42));
        checker.watch(value.get());
        jsc_value_object_set_property(foo.get(), "prop_1", value.get());
        result = adoptGRef(jsc_context_evaluate(context.get(), "f.prop_1", -1));
        checker.watch(result.get());
        g_assert_true(value.get() == result.get());

        result = adoptGRef(jsc_context_evaluate(context.get(), "f.prop_2 = 35", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpuint(jsc_value_to_int32(result.get()), ==, 35);

        result = adoptGRef(jsc_context_evaluate(context.get(), "'prop_2' in f.__proto__", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_false(jsc_value_to_boolean(result.get()));

        result = adoptGRef(jsc_context_evaluate(context.get(), "'prop_2' in f", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));

        result = adoptGRef(jsc_context_evaluate(context.get(), "f.prop_enum_1 = 250", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpuint(jsc_value_to_int32(result.get()), ==, 250);
        result = adoptGRef(jsc_context_evaluate(context.get(), "f.prop_enum_2 = 450", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpuint(jsc_value_to_int32(result.get()), ==, 450);

        properties.reset(jsc_value_object_enumerate_properties(foo.get()));
        g_assert_cmpuint(g_strv_length(properties.get()), ==, 2);
        g_assert_cmpstr(properties.get()[0], ==, "prop_enum_1");
        g_assert_cmpstr(properties.get()[1], ==, "prop_enum_2");
        g_assert_null(properties.get()[2]);

        g_assert_null(jsc_context_get_exception(context.get()));
        bool didThrow = false;
        g_assert_throw_begin(exceptionHandler, didThrow);
        result = adoptGRef(jsc_context_evaluate(context.get(), "f.prop_throw_on_get", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_undefined(result.get()));
        g_assert_did_throw(exceptionHandler, didThrow);
        g_assert_null(jsc_context_get_exception(context.get()));

        didThrow = false;
        g_assert_throw_begin(exceptionHandler, didThrow);
        result = adoptGRef(jsc_context_evaluate(context.get(), "f.prop_3 = 'not a number'", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_undefined(result.get()));
        g_assert_did_throw(exceptionHandler, didThrow);
        g_assert_null(jsc_context_get_exception(context.get()));

        didThrow = false;
        g_assert_throw_begin(exceptionHandler, didThrow);
        jsc_value_object_delete_property(foo.get(), "prop_throw_on_delete");
        g_assert_did_throw(exceptionHandler, didThrow);
        g_assert_null(jsc_context_get_exception(context.get()));

        jsc_context_throw(context.get(), "Fake exception");
        GRefPtr<JSCException> previousException = jsc_context_get_exception(context.get());
        checker.watch(previousException.get());
        didThrow = false;
        g_assert_throw_begin(exceptionHandler, didThrow);
        result = adoptGRef(jsc_context_evaluate(context.get(), "f.prop_throw_on_get", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_undefined(result.get()));
        g_assert_did_throw(exceptionHandler, didThrow);
        g_assert_true(jsc_context_get_exception(context.get()) == previousException.get());

        didThrow = false;
        g_assert_throw_begin(exceptionHandler, didThrow);
        result = adoptGRef(jsc_context_evaluate(context.get(), "f.prop_3 = 'not a number'", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_undefined(result.get()));
        g_assert_did_throw(exceptionHandler, didThrow);
        g_assert_true(jsc_context_get_exception(context.get()) == previousException.get());

        didThrow = false;
        g_assert_throw_begin(exceptionHandler, didThrow);
        jsc_value_object_delete_property(foo.get(), "prop_throw_on_delete");
        g_assert_did_throw(exceptionHandler, didThrow);
        g_assert_true(jsc_context_get_exception(context.get()) == previousException.get());
    }
}

typedef struct {
    Foo parent;
    int bar;
} Bar;

static Bar* barCreate()
{
    Bar* bar = g_new0(Bar, 1);
    new (bar) Bar();
    return bar;
}

static void barFree(Bar* bar)
{
    bar->~Bar();
    g_free(bar);
}

static void setBar(Bar* bar, int value)
{
    bar->bar = value;
}

static int getBar(Bar* bar)
{
    return bar->bar;
}

static JSCClassVTable barVTable = {
    // get_property
    nullptr,
    // set_property
    nullptr,
    // has_property
    nullptr,
    // delete_property
    nullptr,
    // enumerate_properties
    [](JSCClass* jscClass, JSCContext* context, gpointer instance) -> char** {
        auto* checker = static_cast<LeakChecker*>(g_object_get_data(G_OBJECT(jscClass), "leak-checker"));
        checker->watch(context);

        auto* properties = static_cast<char**>(g_malloc0(2 * sizeof(char*)));
        properties[0] = g_strdup("bar");
        return properties;
    },
    // padding
    nullptr, nullptr, nullptr, nullptr
};

static void testJSCPrototypes()
{
    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        JSCClass* fooClass = jsc_context_register_class(context.get(), "Foo", nullptr, nullptr, reinterpret_cast<GDestroyNotify>(fooFree));
        checker.watch(fooClass);
        g_assert_false(jsc_class_get_parent(fooClass));
        GRefPtr<JSCValue> fooConstructor = adoptGRef(jsc_class_add_constructor(fooClass, nullptr, G_CALLBACK(fooCreate), nullptr, nullptr, G_TYPE_POINTER, 0, G_TYPE_NONE));
        checker.watch(fooConstructor.get());
        g_assert_true(jsc_value_is_constructor(fooConstructor.get()));
        jsc_context_set_value(context.get(), jsc_class_get_name(fooClass), fooConstructor.get());
        jsc_class_add_method(fooClass, "multiply", G_CALLBACK(multiplyFoo), nullptr, nullptr, G_TYPE_NONE, 1, G_TYPE_INT);
        jsc_class_add_property(fooClass, "foo", G_TYPE_INT, G_CALLBACK(getFoo), G_CALLBACK(setFoo), nullptr, nullptr);

        JSCClass* barClass = jsc_context_register_class(context.get(), "Bar", fooClass, nullptr, reinterpret_cast<GDestroyNotify>(barFree));
        checker.watch(barClass);
        g_assert_true(jsc_class_get_parent(barClass) == fooClass);
        GRefPtr<JSCValue> barConstructor = adoptGRef(jsc_class_add_constructor(barClass, nullptr, G_CALLBACK(barCreate), nullptr, nullptr, G_TYPE_POINTER, 0, G_TYPE_NONE));
        checker.watch(barConstructor.get());
        g_assert_true(jsc_value_is_constructor(barConstructor.get()));
        jsc_context_set_value(context.get(), jsc_class_get_name(barClass), barConstructor.get());
        jsc_class_add_property(barClass, "bar", G_TYPE_INT, G_CALLBACK(getBar), G_CALLBACK(setBar), nullptr, nullptr);

        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "f = new Foo(); b = new Bar();", -1));
        checker.watch(result.get());

        result = adoptGRef(jsc_context_evaluate(context.get(), "f.__proto__ == Foo.prototype", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));
        result = adoptGRef(jsc_context_evaluate(context.get(), "b.__proto__ == Bar.prototype", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));
        result = adoptGRef(jsc_context_evaluate(context.get(), "b.__proto__.__proto__ == Foo.prototype", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));

        GRefPtr<JSCValue> foo = adoptGRef(jsc_context_get_value(context.get(), "f"));
        checker.watch(foo.get());
        g_assert_true(jsc_value_is_object(foo.get()));
        g_assert_true(jsc_value_object_is_instance_of(foo.get(), jsc_class_get_name(fooClass)));
        g_assert_false(jsc_value_object_is_instance_of(foo.get(), jsc_class_get_name(barClass)));
        result = adoptGRef(jsc_context_evaluate(context.get(), "f instanceof Foo", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));
        result = adoptGRef(jsc_context_evaluate(context.get(), "f instanceof Bar", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_false(jsc_value_to_boolean(result.get()));
        g_assert_true(jsc_value_object_has_property(foo.get(), "foo"));
        g_assert_false(jsc_value_object_has_property(foo.get(), "bar"));
        GUniquePtr<char*> properties(jsc_value_object_enumerate_properties(foo.get()));
        g_assert_null(properties.get());

        GRefPtr<JSCValue> bar = adoptGRef(jsc_context_get_value(context.get(), "b"));
        checker.watch(bar.get());
        g_assert_true(jsc_value_is_object(bar.get()));
        g_assert_true(jsc_value_object_is_instance_of(bar.get(), jsc_class_get_name(barClass)));
        g_assert_true(jsc_value_object_is_instance_of(bar.get(), jsc_class_get_name(fooClass)));

        result = adoptGRef(jsc_context_evaluate(context.get(), "b instanceof Bar", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));
        result = adoptGRef(jsc_context_evaluate(context.get(), "b instanceof Foo", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_boolean(result.get()));
        g_assert_true(jsc_value_to_boolean(result.get()));
        g_assert_true(jsc_value_object_has_property(bar.get(), "bar"));
        g_assert_true(jsc_value_object_has_property(bar.get(), "foo"));
        properties.reset(jsc_value_object_enumerate_properties(bar.get()));
        g_assert_null(properties.get());

        result = adoptGRef(jsc_context_evaluate(context.get(), "b.bar = 25; b.foo = 42;", -1));
        checker.watch(result.get());

        GRefPtr<JSCValue> value = adoptGRef(jsc_context_evaluate(context.get(), "b.bar", -1));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 25);
        value = adoptGRef(jsc_context_evaluate(context.get(), "b.foo", -1));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 42);

        result = adoptGRef(jsc_context_evaluate(context.get(), "b.multiply(2)", -1));
        checker.watch(result.get());
        value = adoptGRef(jsc_context_evaluate(context.get(), "b.foo", -1));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 84);
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        JSCClass* fooClass = jsc_context_register_class(context.get(), "Foo", nullptr, &fooVTable, reinterpret_cast<GDestroyNotify>(fooFree));
        checker.watch(fooClass);
        g_object_set_data(G_OBJECT(fooClass), "leak-checker", &checker);

        GRefPtr<JSCValue> fooConstructor = adoptGRef(jsc_class_add_constructor(fooClass, nullptr, G_CALLBACK(fooCreate), nullptr, nullptr, G_TYPE_POINTER, 0, G_TYPE_NONE));
        checker.watch(fooConstructor.get());
        g_assert_true(jsc_value_is_constructor(fooConstructor.get()));
        jsc_context_set_value(context.get(), jsc_class_get_name(fooClass), fooConstructor.get());
        jsc_class_add_property(fooClass, "foo", G_TYPE_INT, G_CALLBACK(getFoo), G_CALLBACK(setFoo), nullptr, nullptr);

        JSCClass* barClass = jsc_context_register_class(context.get(), "Bar", fooClass, &barVTable, reinterpret_cast<GDestroyNotify>(barFree));
        checker.watch(barClass);
        g_object_set_data(G_OBJECT(barClass), "leak-checker", &checker);
        g_assert_true(jsc_class_get_parent(barClass) == fooClass);
        GRefPtr<JSCValue> barConstructor = adoptGRef(jsc_class_add_constructor(barClass, nullptr, G_CALLBACK(barCreate), nullptr, nullptr, G_TYPE_POINTER, 0, G_TYPE_NONE));
        checker.watch(barConstructor.get());
        g_assert_true(jsc_value_is_constructor(barConstructor.get()));
        jsc_context_set_value(context.get(), jsc_class_get_name(barClass), barConstructor.get());
        jsc_class_add_property(barClass, "bar", G_TYPE_INT, G_CALLBACK(getBar), G_CALLBACK(setBar), nullptr, nullptr);

        GRefPtr<JSCValue> bar = adoptGRef(jsc_context_evaluate(context.get(), "b = new Bar();", -1));
        checker.watch(bar.get());
        g_assert_true(jsc_value_is_object(bar.get()));
        g_assert_true(jsc_value_object_has_property(bar.get(), "bar"));
        g_assert_true(jsc_value_object_has_property(bar.get(), "foo"));

        g_assert_true(jsc_value_object_has_property(bar.get(), "prop_whatever"));
        g_assert_false(jsc_value_object_has_property(bar.get(), "whatever_prop"));

        GUniquePtr<char*> properties(jsc_value_object_enumerate_properties(bar.get()));
        g_assert_cmpuint(g_strv_length(properties.get()), ==, 1);
        g_assert_cmpstr(properties.get()[0], ==, "bar");
        g_assert_null(properties.get()[1]);

        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "b.prop_1", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpuint(jsc_value_to_int32(result.get()), ==, 0);

        GRefPtr<JSCValue> value = adoptGRef(jsc_value_object_get_property(bar.get(), "prop_1"));
        checker.watch(value.get());
        g_assert_true(value.get() == result.get());

        g_assert_true(jsc_value_object_delete_property(bar.get(), "prop_1"));
        g_assert_true(jsc_value_object_has_property(bar.get(), "prop_1"));
        value = adoptGRef(jsc_value_object_get_property(bar.get(), "prop_1"));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpuint(jsc_value_to_int32(value.get()), ==, 0);

        result = adoptGRef(jsc_context_evaluate(context.get(), "b.prop_cant_delete = 125", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpuint(jsc_value_to_int32(result.get()), ==, 125);
        jsc_value_object_delete_property(bar.get(), "prop_cant_delete");
        g_assert_true(jsc_value_object_has_property(bar.get(), "prop_cant_delete"));
        value = adoptGRef(jsc_value_object_get_property(bar.get(), "prop_cant_delete"));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpuint(jsc_value_to_int32(value.get()), ==, 125);

        result = adoptGRef(jsc_context_evaluate(context.get(), "b.prop_enum_1 = 250", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpuint(jsc_value_to_int32(result.get()), ==, 250);
        result = adoptGRef(jsc_context_evaluate(context.get(), "b.prop_enum_2 = 450", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_number(result.get()));
        g_assert_cmpuint(jsc_value_to_int32(result.get()), ==, 450);

        properties.reset(jsc_value_object_enumerate_properties(bar.get()));
        g_assert_cmpuint(g_strv_length(properties.get()), ==, 3);
        g_assert_cmpstr(properties.get()[0], ==, "bar");
        g_assert_cmpstr(properties.get()[1], ==, "prop_enum_1");
        g_assert_cmpstr(properties.get()[2], ==, "prop_enum_2");
        g_assert_null(properties.get()[3]);
    }
}

static void createError()
{
    jsc_context_throw(jsc_context_get_current(), "API exception");
}

static void testJSCExceptions()
{
    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        g_assert_false(jsc_context_get_exception(context.get()));

        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "foo", -1));
        checker.watch(result.get());
        // By default exceptions are not caught.
        g_assert_true(jsc_value_is_undefined(result.get()));
        auto* exception = jsc_context_get_exception(context.get());
        g_assert_true(JSC_IS_EXCEPTION(exception));
        checker.watch(exception);
        g_assert_cmpstr(jsc_exception_get_message(exception), ==, "Can't find variable: foo");
        g_assert_cmpuint(jsc_exception_get_line_number(exception), ==, 1);
        g_assert_false(jsc_exception_get_source_uri(exception));

        jsc_context_clear_exception(context.get());
        g_assert_null(jsc_context_get_exception(context.get()));
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        g_assert_false(jsc_context_get_exception(context.get()));

        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "f = 25;\nfoo;", -1));
        checker.watch(result.get());

        g_assert_true(jsc_value_is_undefined(result.get()));
        auto* exception = jsc_context_get_exception(context.get());
        g_assert_true(JSC_IS_EXCEPTION(exception));
        checker.watch(exception);
        g_assert_cmpuint(jsc_exception_get_line_number(exception), ==, 2);

        jsc_context_clear_exception(context.get());
        g_assert_null(jsc_context_get_exception(context.get()));
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        g_assert_false(jsc_context_get_exception(context.get()));

        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate_with_source_uri(context.get(), "foo", -1, "file:///foo/script.js"));
        checker.watch(result.get());

        g_assert_true(jsc_value_is_undefined(result.get()));
        auto* exception = jsc_context_get_exception(context.get());
        g_assert_true(JSC_IS_EXCEPTION(exception));
        checker.watch(exception);
        g_assert_cmpstr(jsc_exception_get_source_uri(exception), ==, "file:///foo/script.js");

        jsc_context_clear_exception(context.get());
        g_assert_null(jsc_context_get_exception(context.get()));
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        g_assert_false(jsc_context_get_exception(context.get()));

        GRefPtr<JSCValue> function = adoptGRef(jsc_value_new_function(context.get(), "createError", G_CALLBACK(createError), nullptr, nullptr, G_TYPE_NONE, 0, G_TYPE_NONE));
        checker.watch(function.get());
        jsc_context_set_value(context.get(), "createError", function.get());

        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "var result; try { createError(); } catch (e) { result = 'Caught exception'; }", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_string(result.get()));
        GUniquePtr<char> resultString(jsc_value_to_string(result.get()));
        g_assert_cmpstr(resultString.get(), ==, "Caught exception");
        g_assert_false(jsc_context_get_exception(context.get()));

        result = adoptGRef(jsc_context_evaluate(context.get(), "var result; createError(); result = 'No exception';", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_undefined(result.get()));
        auto* exception = jsc_context_get_exception(context.get());
        g_assert_true(JSC_IS_EXCEPTION(exception));
        checker.watch(exception);
        g_assert_cmpstr(jsc_exception_get_message(exception), ==, "API exception");
        g_assert_cmpuint(jsc_exception_get_line_number(exception), ==, 1);
        g_assert_false(jsc_exception_get_source_uri(exception));

        jsc_context_clear_exception(context.get());
        g_assert_null(jsc_context_get_exception(context.get()));
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        g_assert_false(jsc_context_get_exception(context.get()));

        struct Test {
            JSCContext* context;
            GRefPtr<JSCException> exception;
            bool wasDeleted;
        };

        Test test = { context.get(), nullptr, false };
        jsc_context_push_exception_handler(context.get(), [](JSCContext* context, JSCException* exception, gpointer userData) {
            auto* test = static_cast<Test*>(userData);
            g_assert_true(context == test->context);
            g_assert_false(test->exception);
            g_assert_false(test->wasDeleted);
            test->exception = exception;
        }, &test, [](gpointer userData) {
            static_cast<Test*>(userData)->wasDeleted = true;
        });

        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "foo", -1));
        checker.watch(result.get());
        // Exception was caught by the user handler.
        g_assert_false(jsc_context_get_exception(context.get()));
        g_assert_true(JSC_IS_EXCEPTION(test.exception.get()));
        checker.watch(test.exception.get());
        g_assert_cmpstr(jsc_exception_get_message(test.exception.get()), ==, "Can't find variable: foo");
        g_assert_cmpuint(jsc_exception_get_line_number(test.exception.get()), ==, 1);
        g_assert_false(jsc_exception_get_source_uri(test.exception.get()));

        g_assert_false(test.wasDeleted);
        jsc_context_pop_exception_handler(context.get());
        g_assert_true(test.wasDeleted);

        test.exception = nullptr;
        test.wasDeleted = false;
        jsc_context_push_exception_handler(context.get(), [](JSCContext* context, JSCException* exception, gpointer userData) {
            auto* test = static_cast<Test*>(userData);
            g_assert_true(context == test->context);
            g_assert_false(test->exception);
            g_assert_false(test->wasDeleted);
            test->exception = exception;
            jsc_context_throw_exception(context, exception);
        }, &test, [](gpointer userData) {
            static_cast<Test*>(userData)->wasDeleted = true;
        });

        result = adoptGRef(jsc_context_evaluate(context.get(), "foo", -1));
        checker.watch(result.get());
        // Exception was handled by the user handler, but not caught.
        auto* exception = jsc_context_get_exception(context.get());
        g_assert_true(JSC_IS_EXCEPTION(exception));
        checker.watch(exception);
        g_assert_true(exception == test.exception.get());

        g_assert_false(test.wasDeleted);
        jsc_context_pop_exception_handler(context.get());
        g_assert_true(test.wasDeleted);

        jsc_context_clear_exception(context.get());
        g_assert_null(jsc_context_get_exception(context.get()));
    }
}

static void testJSCPromises()
{
    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        GRefPtr<JSCValue> promise = adoptGRef(jsc_context_get_value(context.get(), "Promise"));
        checker.watch(promise.get());
        g_assert_true(jsc_value_is_function(promise.get()));

        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "typeof Promise", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_string(result.get()));
        GUniquePtr<char> resultString(jsc_value_to_string(result.get()));
        g_assert_cmpstr(resultString.get(), ==, "function");

        result = adoptGRef(jsc_context_evaluate(context.get(), "result = 0; Promise.resolve(42).then(function (value) { result = value; });", -1));
        checker.watch(result.get());

        GRefPtr<JSCValue> value = adoptGRef(jsc_context_get_value(context.get(), "result"));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 42);
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        JSCClass* fooClass = jsc_context_register_class(context.get(), "Foo", nullptr, nullptr, reinterpret_cast<GDestroyNotify>(fooFree));
        checker.watch(fooClass);
        g_assert_false(jsc_class_get_parent(fooClass));
        GRefPtr<JSCValue> fooConstructor = adoptGRef(jsc_class_add_constructor(fooClass, nullptr, G_CALLBACK(fooCreate), nullptr, nullptr, G_TYPE_POINTER, 0, G_TYPE_NONE));
        checker.watch(fooConstructor.get());
        g_assert_true(jsc_value_is_constructor(fooConstructor.get()));
        jsc_context_set_value(context.get(), jsc_class_get_name(fooClass), fooConstructor.get());
        jsc_class_add_method(fooClass, "getMultiplyFooAsync", G_CALLBACK(getMultiplyFooAsync), &checker, nullptr, JSC_TYPE_VALUE, 1, G_TYPE_INT);
        jsc_class_add_property(fooClass, "foo", G_TYPE_INT, G_CALLBACK(getFoo), G_CALLBACK(setFoo), nullptr, nullptr);

        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "result = 0; f = new Foo(); f.foo = 5; f.getMultiplyFooAsync(2).then(function (value) { result = value; }, function (error) { result = -1; });", -1));
        checker.watch(result.get());

        GRefPtr<JSCValue> value = adoptGRef(jsc_context_get_value(context.get(), "result"));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, 10);

        result = adoptGRef(jsc_context_evaluate(context.get(), "result = 0; f.getMultiplyFooAsync(0).then(function (value) { result = value; }, function (error) { result = -1; });", -1));
        checker.watch(result.get());
        value = adoptGRef(jsc_context_get_value(context.get(), "result"));
        checker.watch(value.get());
        g_assert_true(jsc_value_is_number(value.get()));
        g_assert_cmpint(jsc_value_to_int32(value.get()), ==, -1);
    }
}

static bool s_fooWasFreed;

static void fooFreeAndLog(Foo* foo)
{
    fooFree(foo);
    s_fooWasFreed = true;
}

static void testJSCGarbageCollector()
{
    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        GRefPtr<JSCValue> object = adoptGRef(jsc_value_new_object(context.get(), nullptr, nullptr));
        checker.watch(object.get());

        GRefPtr<JSCValue> foo = adoptGRef(jsc_value_new_number(context.get(), 25));
        checker.watch(foo.get());
        g_assert_true(jsc_value_is_number(foo.get()));
        g_assert_cmpint(jsc_value_to_int32(foo.get()), ==, 25);
        jsc_value_object_set_property(object.get(), "foo", foo.get());

        jsc_context_set_value(context.get(), "f", object.get());
        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "f", -1));
        checker.watch(result.get());
        g_assert_true(object.get() == result.get());

        object = nullptr;
        foo = nullptr;
        result = nullptr;
        jscContextGarbageCollect(context.get());
        object = adoptGRef(jsc_context_get_value(context.get(), "f"));
        checker.watch(object.get());
        g_assert_true(jsc_value_is_object(object.get()));
        foo = adoptGRef(jsc_context_evaluate(context.get(), "f.foo", -1));
        checker.watch(foo.get());
        g_assert_true(jsc_value_is_number(foo.get()));
        g_assert_cmpint(jsc_value_to_int32(foo.get()), ==, 25);

        result = adoptGRef(jsc_context_evaluate(context.get(), "f = undefined", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_undefined(result.get()));

        jscContextGarbageCollect(context.get());

        g_assert_true(jsc_value_is_object(object.get()));
        jsc_context_set_value(context.get(), "f", object.get());
        result = adoptGRef(jsc_context_evaluate(context.get(), "f", -1));
        checker.watch(result.get());
        g_assert_true(object.get() == result.get());
        foo = adoptGRef(jsc_context_evaluate(context.get(), "f.foo", -1));
        checker.watch(foo.get());
        g_assert_true(jsc_value_is_number(foo.get()));
        g_assert_cmpint(jsc_value_to_int32(foo.get()), ==, 25);
    }

    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        s_fooWasFreed = false;

        JSCClass* jscClass = jsc_context_register_class(context.get(), "Foo", nullptr, nullptr, reinterpret_cast<GDestroyNotify>(fooFreeAndLog));
        checker.watch(jscClass);
        g_assert_false(jsc_class_get_parent(jscClass));

        GRefPtr<JSCValue> constructor = adoptGRef(jsc_class_add_constructor(jscClass, nullptr, G_CALLBACK(fooCreate), nullptr, nullptr, G_TYPE_POINTER, 0, G_TYPE_NONE));
        checker.watch(constructor.get());
        g_assert_true(jsc_value_is_constructor(constructor.get()));
        jsc_context_set_value(context.get(), jsc_class_get_name(jscClass), constructor.get());
        GRefPtr<JSCValue> object = adoptGRef(jsc_context_evaluate(context.get(), "f = new Foo();", -1));
        checker.watch(object.get());
        g_assert_true(jsc_value_is_object(object.get()));
        g_assert_true(jsc_value_object_is_instance_of(object.get(), jsc_class_get_name(jscClass)));

        GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "f", -1));
        checker.watch(result.get());
        g_assert_true(object.get() == result.get());

        result = adoptGRef(jsc_context_evaluate(context.get(), "f = undefined", -1));
        checker.watch(result.get());
        g_assert_true(jsc_value_is_undefined(result.get()));

        g_assert_false(s_fooWasFreed);
        jscContextGarbageCollect(context.get());
        g_assert_false(s_fooWasFreed);

        object = nullptr;
        g_assert_false(s_fooWasFreed);
        jscContextGarbageCollect(context.get());
        g_assert_true(s_fooWasFreed);
    }
}

static void weakValueClearedCallback(JSCWeakValue* weakValue, bool* weakValueCleared)
{
    *weakValueCleared = true;
    g_assert_null(jsc_weak_value_get_value(weakValue));
}

static void testJSCWeakValue()
{
    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        GRefPtr<JSCValue> object = adoptGRef(jsc_value_new_object(context.get(), nullptr, nullptr));
        checker.watch(object.get());

        GRefPtr<JSCWeakValue> weak = adoptGRef(jsc_weak_value_new(object.get()));
        checker.watch(weak.get());
        bool weakValueCleared = false;
        g_signal_connect(weak.get(), "cleared", G_CALLBACK(weakValueClearedCallback), &weakValueCleared);

        jsc_context_set_value(context.get(), "foo", object.get());
        jscContextGarbageCollect(context.get());
        g_assert_false(weakValueCleared);

        GRefPtr<JSCValue> foo = adoptGRef(jsc_context_get_value(context.get(), "foo"));
        checker.watch(foo.get());
        g_assert_true(object.get() == foo.get());

        GRefPtr<JSCValue> weakFoo = adoptGRef(jsc_weak_value_get_value(weak.get()));
        checker.watch(weakFoo.get());
        g_assert_true(foo.get() == weakFoo.get());

        GRefPtr<JSCValue> undefinedValue = adoptGRef(jsc_value_new_undefined(context.get()));
        checker.watch(undefinedValue.get());
        jsc_context_set_value(context.get(), "foo", undefinedValue.get());
        weakFoo = nullptr;
        foo = nullptr;
        object = nullptr;

        // The value is still reachable, but unprotected.
        g_assert_false(weakValueCleared);
        weakFoo = adoptGRef(jsc_weak_value_get_value(weak.get()));
        checker.watch(weakFoo.get());
        g_assert_true(jsc_value_is_object(weakFoo.get()));
        weakFoo = nullptr;

        jscContextGarbageCollect(context.get());
        g_assert_true(weakValueCleared);
        g_assert_null(jsc_weak_value_get_value(weak.get()));
    }

    {
        LeakChecker checker;
        GRefPtr<JSCWeakValue> weakObject;
        bool weakValueCleared = false;
        {
            GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
            checker.watch(context.get());
            ExceptionHandler exceptionHandler(context.get());

            GRefPtr<JSCValue> object = adoptGRef(jsc_context_evaluate(context.get(), "obj = {};", -1));
            checker.watch(object.get());
            g_assert_true(JSC_IS_VALUE(object.get()));
            g_assert_true(jsc_value_is_object(object.get()));

            weakObject = adoptGRef(jsc_weak_value_new(object.get()));
            checker.watch(weakObject.get());
            g_signal_connect(weakObject.get(), "cleared", G_CALLBACK(weakValueClearedCallback), &weakValueCleared);

            object = adoptGRef(jsc_context_evaluate(context.get(), "obj = null", -1));
            checker.watch(object.get());
            g_assert_false(weakValueCleared);
        }

        g_assert_true(weakValueCleared);
        g_assert_null(jsc_weak_value_get_value(weakObject.get()));
    }

    {
        LeakChecker checker;
        GRefPtr<JSCWeakValue> weakObj;
        bool weakObjValueCleared = false;
        GRefPtr<JSCWeakValue> weakStr;
        bool weakStrValueCleared = false;
        GRefPtr<JSCWeakValue> weakPrimitive;
        bool weakPrimitiveValueCleared = false;
        {
            GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
            checker.watch(context.get());
            ExceptionHandler exceptionHandler(context.get());

            GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(), "obj = { 'foo' : 'bar' }; str = 'Hello World'; primitive = 25;", -1));
            checker.watch(result.get());

            GRefPtr<JSCValue> value = adoptGRef(jsc_context_get_value(context.get(), "obj"));
            checker.watch(value.get());
            weakObj = adoptGRef(jsc_weak_value_new(value.get()));
            checker.watch(weakObj.get());
            g_signal_connect(weakObj.get(), "cleared", G_CALLBACK(weakValueClearedCallback), &weakObjValueCleared);

            value = adoptGRef(jsc_context_get_value(context.get(), "str"));
            checker.watch(value.get());
            weakStr = adoptGRef(jsc_weak_value_new(value.get()));
            checker.watch(weakStr.get());
            g_signal_connect(weakStr.get(), "cleared", G_CALLBACK(weakValueClearedCallback), &weakStrValueCleared);

            value = adoptGRef(jsc_context_get_value(context.get(), "primitive"));
            checker.watch(value.get());
            weakPrimitive = adoptGRef(jsc_weak_value_new(value.get()));
            checker.watch(weakPrimitive.get());
            g_signal_connect(weakPrimitive.get(), "cleared", G_CALLBACK(weakValueClearedCallback), &weakPrimitiveValueCleared);

            value = nullptr;
            jscContextGarbageCollect(context.get());
            g_assert_false(weakObjValueCleared);
            g_assert_false(weakStrValueCleared);
            g_assert_false(weakPrimitiveValueCleared);

            value = adoptGRef(jsc_weak_value_get_value(weakObj.get()));
            checker.watch(value.get());
            g_assert_true(jsc_value_is_object(value.get()));

            value = adoptGRef(jsc_weak_value_get_value(weakStr.get()));
            checker.watch(value.get());
            g_assert_true(jsc_value_is_string(value.get()));

            value = adoptGRef(jsc_weak_value_get_value(weakPrimitive.get()));
            checker.watch(value.get());
            g_assert_true(jsc_value_is_number(value.get()));
            value = nullptr;

            result = adoptGRef(jsc_context_evaluate(context.get(), "str = undefined", -1));
            checker.watch(result.get());
            jscContextGarbageCollect(context.get());

            g_assert_true(weakStrValueCleared);
            g_assert_false(weakObjValueCleared);
            g_assert_false(weakPrimitiveValueCleared);
            g_assert_null(jsc_weak_value_get_value(weakStr.get()));

            result = adoptGRef(jsc_context_evaluate(context.get(), "f = undefined", -1));
            checker.watch(result.get());
            jscContextGarbageCollect(context.get());

            // Non-string primitve values are not garbage collected, the weak value
            // will be cleared when the global object is destroyed.
            g_assert_false(weakPrimitiveValueCleared);
            g_assert_false(weakObjValueCleared);
            g_assert_true(weakStrValueCleared);

            value = adoptGRef(jsc_weak_value_get_value(weakPrimitive.get()));
            checker.watch(value.get());
            g_assert_true(jsc_value_is_number(value.get()));
            value = nullptr;

            result = adoptGRef(jsc_context_evaluate(context.get(), "obj = undefined", -1));
            checker.watch(result.get());
            jscContextGarbageCollect(context.get());

            g_assert_true(weakObjValueCleared);
            g_assert_true(weakStrValueCleared);
            g_assert_false(weakPrimitiveValueCleared);
            g_assert_null(jsc_weak_value_get_value(weakObj.get()));
            weakObjValueCleared = false;
            weakStrValueCleared = false;
        }

        // Context is now destroyed, only the primitive value should be notified.
        g_assert_true(weakPrimitiveValueCleared);
        g_assert_false(weakObjValueCleared);
        g_assert_false(weakStrValueCleared);
        g_assert_null(jsc_weak_value_get_value(weakPrimitive.get()));
    }
}

static void testsJSCVirtualMachine()
{
    {
        LeakChecker checker;
        GRefPtr<JSCContext> context = adoptGRef(jsc_context_new());
        checker.watch(context.get());
        ExceptionHandler exceptionHandler(context.get());

        auto thread = Thread::create("JSCVirtualMachineTest", [&] {
            JSCClass* jscClass = jsc_context_register_class(context.get(), "Foo", nullptr, nullptr, reinterpret_cast<GDestroyNotify>(fooFreeAndLog));
            checker.watch(jscClass);
            g_assert_false(jsc_class_get_parent(jscClass));

            GRefPtr<JSCValue> constructor = adoptGRef(jsc_class_add_constructor(jscClass, nullptr, G_CALLBACK(fooCreate), nullptr, nullptr, G_TYPE_POINTER, 0, G_TYPE_NONE));
            checker.watch(constructor.get());
            g_assert_true(jsc_value_is_constructor(constructor.get()));
            jsc_context_set_value(context.get(), jsc_class_get_name(jscClass), constructor.get());

            GRefPtr<JSCValue> object = adoptGRef(jsc_context_evaluate(context.get(), "f = new Foo();", -1));
            checker.watch(object.get());
            g_assert_true(jsc_value_get_context(object.get()) == context.get());
            g_assert_true(jsc_value_is_object(object.get()));
            g_assert_true(jsc_value_object_is_instance_of(object.get(), jsc_class_get_name(jscClass)));
        });
        thread->waitForCompletion();

        GRefPtr<JSCValue> object = adoptGRef(jsc_context_get_value(context.get(), "f"));
        checker.watch(object.get());
        g_assert_true(jsc_value_is_object(object.get()));

        jscContextGarbageCollect(context.get());
    }

    {
        Vector<Ref<Thread>, 5> threads;
        bool ok = true;
        for (unsigned i = 0; i < 5; ++i) {
            threads.append(Thread::create("JSCVirtualMachineTest", [&ok] {
                GRefPtr<JSCVirtualMachine> vm = adoptGRef(jsc_virtual_machine_new());
                GRefPtr<JSCContext> context = adoptGRef(jsc_context_new_with_virtual_machine(vm.get()));
                GRefPtr<JSCValue> result = adoptGRef(jsc_context_evaluate(context.get(),
                    "var array = [{}];\n"
                    "for (var i = 0; i < 20; ++i) {\n"
                    "    var newArray = new Array(array.length * 2);\n"
                    "    for (var j = 0; j < newArray.length; ++j)\n"
                    "        newArray[j] = {parent: array[j / 2]};\n"
                    "    array = newArray;\n"
                    "}\n", -1
                ));
                g_assert_true(jsc_value_get_context(result.get()) == context.get());
                if (auto* exception = jsc_context_get_exception(context.get())) {
                    g_message("Uncaught exception: %s", jsc_exception_get_message(exception));
                    ok = false;
                }
                GRefPtr<JSCValue> value = adoptGRef(jsc_context_get_value(context.get(), "array"));
                g_assert_true(jsc_value_get_context(value.get()) == context.get());
                if (!jsc_value_is_object(value.get())) {
                    g_message("Did not find \"array\" variable");
                    ok = false;
                }
                jscContextGarbageCollect(context.get());
            }));
        }

        for (auto& thread : threads)
            thread->waitForCompletion();

        g_assert_true(ok);
    }
}

#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
static void testsJSCAutocleanups()
{
    LeakChecker checker;
    g_autoptr(JSCVirtualMachine) vm = jsc_virtual_machine_new();
    checker.watch(vm);
    g_assert_true(JSC_IS_VIRTUAL_MACHINE(vm));

    g_autoptr(JSCContext) context = jsc_context_new_with_virtual_machine(vm);
    checker.watch(context);
    g_assert_true(JSC_IS_CONTEXT(context));

    g_autoptr(JSCValue) value = jsc_context_evaluate(context, "v = 25", -1);
    checker.watch(value);
    g_assert_true(JSC_IS_VALUE(value));
    g_assert_true(jsc_value_is_number(value));
    g_assert_cmpint(jsc_value_to_int32(value), ==, 25);

    g_autoptr(JSCException) exception = jsc_exception_new(context, "API error");
    checker.watch(exception);
    g_assert_true(JSC_IS_EXCEPTION(exception));
    jsc_context_throw_exception(context, exception);
}
#endif

int main(int argc, char** argv)
{
    g_test_init(&argc, &argv, nullptr);

    g_test_add_func("/jsc/basic", testJSCBasic);
    g_test_add_func("/jsc/types", testJSCTypes);
    g_test_add_func("/jsc/function", testJSCFunction);
    g_test_add_func("/jsc/object", testJSCObject);
    g_test_add_func("/jsc/class", testJSCClass);
    g_test_add_func("/jsc/prototypes", testJSCPrototypes);
    g_test_add_func("/jsc/exceptions", testJSCExceptions);
    g_test_add_func("/jsc/promises", testJSCPromises);
    g_test_add_func("/jsc/garbage-collector", testJSCGarbageCollector);
    g_test_add_func("/jsc/weak-value", testJSCWeakValue);
    g_test_add_func("/jsc/vm", testsJSCVirtualMachine);
#ifdef G_DEFINE_AUTOPTR_CLEANUP_FUNC
    g_test_add_func("/jsc/autocleanups", testsJSCAutocleanups);
#endif

    return g_test_run();
}