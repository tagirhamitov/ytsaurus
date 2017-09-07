#include "yson_lazy_map.h"

#include <structmember.h>

#ifdef __GNUC__
#pragma GCC diagnostic ignored "-Wwrite-strings"
#endif

namespace NYT {
namespace NYTree {

////////////////////////////////////////////////////////////////////////////////

void LazyDictCopy(TLazyDict* source, TLazyDict* destination, bool deep)
{
    static Py::Callable deepcopyFunction;
    if (deepcopyFunction.isNone()) {
        auto ptr = PyImport_ImportModule("copy");
        if (!ptr) {
            throw Py::RuntimeError("Failed to import module copy");
        }
        auto module = Py::Object(ptr);
        deepcopyFunction = Py::Callable(Py::GetAttr(module, "deepcopy"));
        deepcopyFunction.increment_reference_count();
    }

    for (const auto& item: *source->GetUnderlyingHashMap()) {
        const auto& key = item.first;
        const auto& value = item.second;

        if (value.Value) {
            if (deep) {
                destination->SetItem(key, deepcopyFunction.apply(Py::TupleN(value.Value.Get())));
            } else {
                destination->SetItem(key, value.Value.Get());
            }
        } else {
            destination->SetItem(key, value.Data);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////

PyObject* LazyYsonMapBaseSubscript(PyObject* object, PyObject* key)
{
    TLazyYsonMapBase* self = reinterpret_cast<TLazyYsonMapBase*>(object);
    auto pyKey = Py::Object(key);
    if (!self->Dict->HasItem(pyKey)) {
        PyErr_SetObject(PyExc_KeyError, key);
        return nullptr;
    }

    auto result = self->Dict->GetItem(pyKey);
    Py_INCREF(result);
    return result;
}

PyObject* LazyYsonMapBaseHasKey(PyObject* object, PyObject* key)
{
    return PyBool_FromLong(LazyYsonMapBaseContains(object, key));
}

PyObject* LazyYsonMapBaseGet(TLazyYsonMapBase* self, PyObject* args, PyObject* kwargs)
{
    Py::Tuple arguments(args);
    Py::Dict kwarguments;

    if (kwargs) {
        kwarguments = Py::Dict(kwargs);
    }
    Py::Object key = NPython::ExtractArgument(arguments, kwarguments, "key");

    Py::Object defaultValue = Py::None();
    if (NPython::HasArgument(arguments, kwarguments, "default")) {
        defaultValue = NPython::ExtractArgument(arguments, kwarguments, "default");
    }

    if (self->Dict->HasItem(key)) {
        auto result = self->Dict->GetItem(key);
        Py_INCREF(result);
        return result;
    }
    return defaultValue.ptr();
}

PyObject* LazyYsonMapBaseClear(TLazyYsonMapBase* self)
{
    self->Dict->Clear();
    Py_RETURN_NONE;
}

PyObject* LazyYsonMapBaseSetDefault(TLazyYsonMapBase* self, PyObject* args, PyObject* kwargs)
{
    Py::Tuple arguments(args);
    Py::Dict kwarguments;

    if (kwargs) {
        kwarguments = Py::Dict(kwargs);
    }
    Py::Object key = NPython::ExtractArgument(arguments, kwarguments, "key");

    PyObject* value = Py_None;
    if (NPython::HasArgument(arguments, kwarguments, "default")) {
        value = NPython::ExtractArgument(arguments, kwarguments, "default").ptr();
    }

    if (!self->Dict->HasItem(key)) {
        self->Dict->SetItem(key, Py::Object(value));
    } else {
        value = self->Dict->GetItem(key);
    }
    return value;
}

PyObject* LazyYsonMapBaseCopy(TLazyYsonMapBase* self)
{
    auto result = LazyYsonMapBaseNew(&TLazyYsonMapBaseType, Py_None, Py_None);
    auto params = self->Dict->GetConsumerParams();

    auto resultObject = reinterpret_cast<TLazyYsonMapBase*>(result);
    LazyYsonMapBaseInit(resultObject, params.ptr(), Py::Dict().ptr());

    LazyDictCopy(self->Dict, resultObject->Dict, false);
    return result;
}

PyObject* LazyYsonMapBaseDeepCopy(TLazyYsonMapBase* self, PyObject* args, PyObject* kwargs)
{
    auto result = LazyYsonMapBaseNew(&TLazyYsonMapBaseType, Py_None, Py_None);
    auto params = self->Dict->GetConsumerParams();

    auto resultObject = reinterpret_cast<TLazyYsonMapBase*>(result);
    LazyYsonMapBaseInit(resultObject, params.ptr(), Py::Dict().ptr());

    LazyDictCopy(self->Dict, resultObject->Dict, true);
    return result;
}

PyObject* LazyYsonMapBaseNew(PyTypeObject* type, PyObject* /* args */, PyObject* /* kwargs */)
{
    TLazyYsonMapBase* self;
    self = reinterpret_cast<TLazyYsonMapBase*>(type->tp_alloc(type, 0));
    return reinterpret_cast<PyObject*>(self);
}

int LazyYsonMapBaseAssSubscript(PyObject* object, PyObject* key, PyObject* value)
{
    TLazyYsonMapBase* self = reinterpret_cast<TLazyYsonMapBase*>(object);
    auto pyKey = Py::Object(key);
    if (value == nullptr) {
        if (!self->Dict->HasItem(pyKey)) {
            PyErr_SetObject(PyExc_KeyError, key);
            return -1;
        }
        self->Dict->DeleteItem(pyKey);
        return 0;
    }
    self->Dict->SetItem(pyKey, Py::Object(value));
    return 0;
}

int LazyYsonMapBaseContains(PyObject* object, PyObject* key)
{
    TLazyYsonMapBase* self = reinterpret_cast<TLazyYsonMapBase*>(object);
    auto pyKey = Py::Object(key);
    if (self->Dict->HasItem(pyKey)) {
        return 1;
    }
    return 0;
}

int LazyYsonMapBaseInit(TLazyYsonMapBase* self, PyObject* args, PyObject* kwargs)
{
    Py::Tuple arguments(args);
    Py::Dict kwarguments(kwargs);

    TNullable<TString> encoding;
    auto arg = NPython::ExtractArgument(arguments, kwarguments, "encoding");
    if (!arg.isNone()) {
        encoding = Py::ConvertStringObjectToString(arg);
    }

    arg = NPython::ExtractArgument(arguments, kwarguments, "always_create_attributes");
    bool alwaysCreateAttributes = Py::Boolean(arg);

    self->Dict = new TLazyDict(alwaysCreateAttributes, encoding);
    return 0;
}

Py_ssize_t LazyYsonMapBaseLength(PyObject* object)
{
    TLazyYsonMapBase* self = reinterpret_cast<TLazyYsonMapBase*>(object);
    return self->Dict->Length();
}

void LazyYsonMapBaseDealloc(TLazyYsonMapBase* self)
{
    delete self->Dict;
    Py_TYPE(self)->tp_free(reinterpret_cast<PyObject*>(self));
}

////////////////////////////////////////////////////////////////////////////////

int LazyYsonMapInit(TLazyYsonMap* self, PyObject* args, PyObject* kwargs)
{
    LazyYsonMapBaseInit(&self->super, args, kwargs);
    LazyYsonMapBaseInit(reinterpret_cast<TLazyYsonMapBase*>(self->Attributes), args, kwargs);
    return 0;
}

PyObject* LazyYsonMapNew(PyTypeObject* type, PyObject* /* args */, PyObject* /* kwargs */)
{
    TLazyYsonMap* self;
    self = reinterpret_cast<TLazyYsonMap*>(type->tp_alloc(type, 0));
    self->Attributes = LazyYsonMapBaseNew(&TLazyYsonMapBaseType, Py_None, Py_None);
    return (PyObject*)self;
}

void LazyYsonMapDealloc(TLazyYsonMap* self)
{
    Py_DECREF(self->Attributes);
    LazyYsonMapBaseDealloc(&self->super);
}

PyObject* LazyYsonMapCopy(TLazyYsonMap* self)
{
    auto result = reinterpret_cast<TLazyYsonMap*>(LazyYsonMapNew(&TLazyYsonMapType, Py_None, Py_None));
    auto params = self->super.Dict->GetConsumerParams();
    LazyYsonMapInit(result, params.ptr(), Py::Dict().ptr());

    LazyDictCopy(self->super.Dict, result->super.Dict, false);
    LazyDictCopy(reinterpret_cast<TLazyYsonMapBase*>(self->Attributes)->Dict,
                 reinterpret_cast<TLazyYsonMapBase*>(result->Attributes)->Dict, false);
    return reinterpret_cast<PyObject*>(result);
}

PyObject* LazyYsonMapDeepCopy(TLazyYsonMap* self, PyObject* args, PyObject* kwargs)
{
    auto result = reinterpret_cast<TLazyYsonMap*>(LazyYsonMapNew(&TLazyYsonMapType, Py_None, Py_None));
    auto params = self->super.Dict->GetConsumerParams();
    LazyYsonMapInit(result, params.ptr(), Py::Dict().ptr());

    LazyDictCopy(self->super.Dict, result->super.Dict, true);
    LazyDictCopy(reinterpret_cast<TLazyYsonMapBase*>(self->Attributes)->Dict,
                 reinterpret_cast<TLazyYsonMapBase*>(result->Attributes)->Dict,
                 true);
    return reinterpret_cast<PyObject*>(result);
}

////////////////////////////////////////////////////////////////////////////////

PyMappingMethods TLazyYsonMapBaseMappingMethods = {
    LazyYsonMapBaseLength,          // mp_length
    LazyYsonMapBaseSubscript,       // mp_subscript
    LazyYsonMapBaseAssSubscript     // mp_ass_subscript
};

PySequenceMethods TLazyYsonMapBaseSequenceMethods = {
    0,                              // sq_length
    0,                              // sq_concat
    0,                              // sq_repeat
    0,                              // sq_item
    0,                              // sq_slice,
    0,                              // sq_ass_item
    0,                              // sq_ass_slice
    LazyYsonMapBaseContains,        // sq_contains
    0,                              // sq_inplace_concat
    0                               // sq_inplace_repeat
};

PyMethodDef LazyYsonMapBaseMethods[] = {
    {"has_key", (PyCFunction)LazyYsonMapBaseHasKey, METH_O, ""},
    {"get", (PyCFunction)LazyYsonMapBaseGet, METH_VARARGS | METH_KEYWORDS, ""},
    {"clear", (PyCFunction)LazyYsonMapBaseClear, METH_NOARGS, ""},
    {"setdefault", (PyCFunction)LazyYsonMapBaseSetDefault, METH_VARARGS | METH_KEYWORDS, ""},
    {"__copy__", (PyCFunction)LazyYsonMapBaseCopy, METH_NOARGS, ""},
    {"__deepcopy__", (PyCFunction)LazyYsonMapBaseDeepCopy, METH_VARARGS | METH_KEYWORDS, ""},
    {nullptr}
};

PyMethodDef LazyYsonMapMethods[] = {
    {"__copy__", (PyCFunction)LazyYsonMapCopy, METH_NOARGS, ""},
    {"__deepcopy__", (PyCFunction)LazyYsonMapDeepCopy, METH_VARARGS | METH_KEYWORDS, ""},
    {nullptr}
};

PyMemberDef LazyYsonMapMembers[] = {
    {"attributes", T_OBJECT_EX, offsetof(TLazyYsonMap, Attributes), 0, ""},
    {nullptr}
};

////////////////////////////////////////////////////////////////////////////////

PyTypeObject TLazyYsonMapType = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    "YsonLazyMap",                  // tp_name
    sizeof(TLazyYsonMap),           // tp_basicsize
    0,                              // tp_itemsize
    (destructor)LazyYsonMapDealloc, // tp_dealloc
    0,                              // tp_print
    0,                              // tp_getattr
    0,                              // tp_setattr
    0,                              // tp_compare
    0,                              // tp_repr
    0,                              // tp_as_number
    0,                              // tp_as_sequence
    0,                              // tp_as_mapping
    0,                              // tp_hash
    0,                              // tp_call
    0,                              // tp_str
    0,                              // tp_getattro
    0,                              // tp_setattro
    0,                              // tp_as_buffer
    Py_TPFLAGS_DEFAULT |
    Py_TPFLAGS_BASETYPE,            // tp_flags
    "YsonLazyMap",                  // tp_doc
    0,                              // tp_traverse
    0,                              // tp_clear
    0,                              // tp_richcompare
    0,                              // tp_weaklistoffset
    0,                              // tp_iter
    0,                              // tp_iternext
    LazyYsonMapMethods,             // tp_methods
    LazyYsonMapMembers,             // tp_members
    0,                              // tp_getset
    &TLazyYsonMapBaseType,          // tp_base
    0,                              // tp_dict
    0,                              // tp_descr_get
    0,                              // tp_descr_set
    0,                              // tp_dictoffset
    (initproc)LazyYsonMapInit,      // tp_init
    0,                              // tp_alloc
    LazyYsonMapNew,                 // tp_new
};

PyTypeObject TLazyYsonMapBaseType = {
    PyVarObject_HEAD_INIT(nullptr, 0)
    "YsonLazyMapBase",                  // tp_name
    sizeof(TLazyYsonMapBase),           // tp_basicsize
    0,                                  // tp_itemsize
    (destructor)LazyYsonMapBaseDealloc, // tp_dealloc
    0,                                  // tp_print
    0,                                  // tp_getattr
    0,                                  // tp_setattr
    0,                                  // tp_compare
    0,                                  // tp_repr
    0,                                  // tp_as_number
    &TLazyYsonMapBaseSequenceMethods,   // tp_as_sequence
    &TLazyYsonMapBaseMappingMethods,    // tp_as_mapping
    PyObject_HashNotImplemented,        // tp_hash
    0,                                  // tp_call
    0,                                  // tp_str
    0,                                  // tp_getattro
    0,                                  // tp_setattro
    0,                                  // tp_as_buffer
    Py_TPFLAGS_DEFAULT |
    Py_TPFLAGS_BASETYPE,                // tp_flags
    "YsonLazyMapBase",                  // tp_doc
    0,                                  // tp_traverse
    0,                                  // tp_clear
    0,                                  // tp_richcompare
    0,                                  // tp_weaklistoffset
    0,                                  // tp_iter
    0,                                  // tp_iternext
    LazyYsonMapBaseMethods,             // tp_methods
    0,                                  // tp_members
    0,                                  // tp_getset
    0,                                  // tp_base
    0,                                  // tp_dict
    0,                                  // tp_descr_get
    0,                                  // tp_descr_set
    0,                                  // tp_dictoffset
    (initproc)LazyYsonMapBaseInit,      // tp_init
    0,                                  // tp_alloc
    LazyYsonMapBaseNew,                 // tp_new
};

////////////////////////////////////////////////////////////////////////////////

bool IsYsonLazyMap(PyObject* object)
{
    return Py_TYPE(object) == &TLazyYsonMapType || Py_TYPE(object) == &TLazyYsonMapBaseType;
}

////////////////////////////////////////////////////////////////////////////////

} // namespace NYTree
} // namespace NYT
