#define PY_SSIZE_T_CLEAN
#include "error_handling.h"
#include <emscripten.h>

#include "hiwire.h"
#include "js2python.h"
#include "jsmemops.h" // for pyproxy.js
// #include "jsproxy.h"
#include "pyproxy.h"
#include "python2js.h"

#include "py/builtin.h"
#include "py/runtime.h"
#include "py/objtype.h"

#define HAS_LENGTH   (1 << 0)
#define HAS_SUBSCR   (1 << 1)
#define HAS_CONTAINS (1 << 3)
#define IS_ITERABLE  (1 << 4)
#define IS_ITERATOR  (1 << 5)
#define IS_AWAITABLE (1 << 6)
#define IS_BUFFER    (1 << 7)
#define IS_CALLABLE  (1 << 8)
#define IS_ASYNC_ITERABLE (1 << 9)
#define IS_ASYNC_ITERATOR (1 << 10)
#define IS_GENERATOR (1 << 11)
#define IS_ASYNC_GENERATOR (1 << 12)

#define Py_ENTER()
#define Py_EXIT()


// We stick objects in here so that the gc will know we need them still
mp_obj_t proxy_dict;

EMSCRIPTEN_KEEPALIVE int
pyproxy_getflags(mp_obj_t pyobj)
{
  const mp_obj_type_t *type = mp_obj_get_type(pyobj);
  int result = 0;

  #define SET_FLAG_IF(flag, cond)                                                \
    if (cond) {                                                                  \
      result |= flag;                                                            \
    }
  

  SET_FLAG_IF(HAS_LENGTH, false);
  SET_FLAG_IF(HAS_SUBSCR, MP_OBJ_TYPE_GET_SLOT_OR_NULL(type, subscr) != NULL);
  SET_FLAG_IF(HAS_CONTAINS, MP_OBJ_TYPE_GET_SLOT_OR_NULL(type, binary_op));
  SET_FLAG_IF(IS_CALLABLE, mp_obj_is_callable(pyobj));
  SET_FLAG_IF(IS_ITERABLE, false);
  SET_FLAG_IF(IS_ITERATOR, false);


  #undef SET_FLAG_IF
  return result;
}


// Use raw EM_JS for the next five commands. We intend to signal a fatal error
// if a JavaScript error is thrown.

EM_JS(int, pyproxy_Check_js, (JsRef x), {
  if (x == 0) {
    return false;
  }
  let val = Hiwire.get_value(x);
  return API.isPyProxy(val);
});

int
pyproxy_Check(JsRef x) {
  return pyproxy_Check_js(x);
}

EM_JS(mp_obj_t, pyproxy_AsPyObject, (JsRef x), {
  if (x == 0) {
    return 0;
  }
  let val = Hiwire.get_value(x);
  if (!API.isPyProxy(val)) {
    return 0;
  }
  return Module.PyProxy_getPtr(val);
});

EM_JS(void, destroy_proxies_js, (JsRef proxies_id, char* msg_ptr), {
  let msg = undefined;
  if (msg_ptr) {
    msg = UTF8ToString(msg_ptr);
  }
  let proxies = Hiwire.get_value(proxies_id);
  for (let px of proxies) {
    Module.pyproxy_destroy(px, msg, false);
  }
});

void
destroy_proxies(JsRef proxies_id, char* msg_ptr) {
  destroy_proxies_js(proxies_id, msg_ptr);
}

EM_JS(void, destroy_proxy, (JsRef proxy_id, char* msg_ptr), {
  let px = Module.hiwire.get_value(proxy_id);
  if (px.$$props.roundtrip) {
    // Don't destroy roundtrip proxies!
    return;
  }
  let msg = undefined;
  if (msg_ptr) {
    msg = UTF8ToString(msg_ptr);
  }
  Module.pyproxy_destroy(px, msg, false);
});


///////////////////////////////////////////////////////////////////////////////
//
// Object protocol wrappers
//
// This section defines wrappers for Python Object protocol API calls that we
// are planning to offer on the PyProxy. Much of this could be written in
// JavaScript instead. Some reasons to do it in C:
//  1. Some CPython APIs are actually secretly macros which cannot be used from
//     JavaScript.
//  2. The code is a bit more concise in C.
//  3. It may be preferable to minimize the number of times we cross between
//     wasm and javascript for performance reasons
//  4. Better separation of functionality: Most of the JavaScript code is
//     boilerpalte. Most of this code here is boilerplate. However, the
//     boilerplate in these C API wwrappers is a bit different than the
//     boilerplate in the javascript wrappers, so we've factored it into two
//     distinct layers of boilerplate.
//
//  Item 1 makes it technically necessary to use these wrappers once in a while.
//  I think all of these advantages outweigh the cost of splitting up the
//  implementation of each feature like this, especially because most of the
//  logic is very boilerplatey, so there isn't much surprising code hidden
//  somewhere else.

EMSCRIPTEN_KEEPALIVE JsRef
_pyproxy_repr(mp_obj_t pyobj)
{
  JsRef repr_js = NULL;

  vstr_t vstr;
  mp_print_t print;
  vstr_init_print(&vstr, 16, &print);
  mp_obj_print_helper(&print, pyobj, PRINT_REPR);

  mp_obj_t mp_str = mp_obj_new_str_from_utf8_vstr(&vstr);
  repr_js = python2js(mp_str);

  return repr_js;
}

// /**
//  * Wrapper for the "proxy.type" getter, which behaves a little bit like
//  * `type(obj)`, but instead of returning the class we just return the name of
//  * the class. The exact behavior is that this usually gives "module.name" but
//  * for builtins just gives "name". So in particular, usually it is equivalent
//  * to:
//  *
//  * `type(x).__module__ + "." + type(x).__name__`
//  *
//  * But other times it behaves like:
//  *
//  * `type(x).__name__`
//  */
EMSCRIPTEN_KEEPALIVE JsRef
_pyproxy_type(mp_obj_t ptrobj)
{
  return hiwire_string_utf8(mp_obj_get_type_str(ptrobj));
}

EMSCRIPTEN_KEEPALIVE int
_pyproxy_hasattr(mp_obj_t pyobj, JsRef jsattr)
{
  mp_obj_t pyattr = js2python(jsattr);
  if(pyattr == 0) {
    // TODO: Set error!
    return -1;
  }
  
  qstr attr = mp_obj_str_get_qstr(pyattr);
  mp_obj_t dest[2];
  mp_load_method_protected(pyobj, attr, dest, false);
  return dest[0] != MP_OBJ_NULL;
}


EMSCRIPTEN_KEEPALIVE JsRef
_pyproxy_getattr(mp_obj_t pyobj, JsRef idkey, JsRef proxyCache)
{
  nlr_buf_t nlr;
  if (nlr_push(&nlr) == 0) {
    mp_obj_t pykey = js2python(idkey);
    // If it's a method, we use the descriptor pointer as the cache key rather
    // than the actual bound method. This allows us to reuse bound methods from
    // the cache.
    // _PyObject_GetMethod will return true and store a descriptor into pydescr if
    // the attribute we are looking up is a method, otherwise it will return false
    // and set pydescr to the actual attribute (in particular, I believe that it
    // will resolve other types of getter descriptors automatically).
    mp_obj_t pyresult = mp_load_attr(pyobj, mp_obj_str_get_qstr(pykey));
    JsRef result = python2js(pyresult);
    nlr_pop();
    return result;
  } else {
    record_traceback(nlr.ret_val);
    return NULL;
  }
}


EMSCRIPTEN_KEEPALIVE void
_pyproxy_setattr(mp_obj_t pyobj, JsRef jsattr, JsRef jsval)
{
  nlr_buf_t nlr;
  if (nlr_push(&nlr) == 0) {
    mp_obj_t pyattr = js2python(jsattr);
    mp_obj_t pyval = js2python(jsval);

    mp_store_attr(pyobj, mp_obj_str_get_qstr(pyattr), pyval);
    nlr_pop();
  } else {
    record_traceback(nlr.ret_val);
  }
}

EMSCRIPTEN_KEEPALIVE void
_pyproxy_delattr(mp_obj_t pyobj, JsRef jsattr)
{
  nlr_buf_t nlr;
  if (nlr_push(&nlr) == 0) {
    mp_obj_t pyattr = js2python(jsattr);

    mp_store_attr(pyobj, mp_obj_str_get_qstr(pyattr), MP_OBJ_NULL);
    nlr_pop();
  } else {
    record_traceback(nlr.ret_val);
  }
}

EMSCRIPTEN_KEEPALIVE JsRef
_pyproxy_getitem(mp_obj_t pyobj, JsRef idkey)
{
  nlr_buf_t nlr;
  if (nlr_push(&nlr) == 0) {
    mp_obj_t pykey = js2python(idkey);
    mp_obj_t pyresult = mp_call_function_2((mp_obj_t*)&mp_op_getitem_obj, pyobj, pykey);
    JsRef result = python2js(pyresult);
    nlr_pop();
    return result;
  } else {
    if (mp_obj_is_subclass_fast(MP_OBJ_FROM_PTR(((mp_obj_base_t *)nlr.ret_val)->type), MP_OBJ_FROM_PTR(&mp_type_KeyError))) {
      // Suppress KeyError and return undefined.
      return Js_undefined;
    } 
    record_traceback(nlr.ret_val);
    return NULL;
  }
};

EMSCRIPTEN_KEEPALIVE void
_pyproxy_setitem(mp_obj_t pyobj, JsRef idkey, JsRef idval)
{
  nlr_buf_t nlr;
  if (nlr_push(&nlr) == 0) {

    mp_obj_t pykey = js2python(idkey);
    mp_obj_t pyval = js2python(idval);
    mp_call_function_n_kw((mp_obj_t*)&mp_op_setitem_obj, 3, 0, (const mp_obj_t[3]){pyobj, pykey, pyval});
    nlr_pop();
  } else {
    record_traceback(nlr.ret_val);
  }
}

EMSCRIPTEN_KEEPALIVE void
_pyproxy_delitem(mp_obj_t pyobj, JsRef idkey)
{
  nlr_buf_t nlr;
  if (nlr_push(&nlr) == 0) {
    mp_obj_t pykey = js2python(idkey);
    mp_call_function_2((mp_obj_t*)&mp_op_delitem_obj, pyobj, pykey);
    nlr_pop();
  } else {
    record_traceback(nlr.ret_val);
  }
}

EMSCRIPTEN_KEEPALIVE int
_pyproxy_contains(mp_obj_t pyobj, JsRef idkey)
{
  nlr_buf_t nlr;
  if (nlr_push(&nlr) == 0) {
    mp_obj_t pykey = js2python(idkey);
    int result = mp_call_function_2((mp_obj_t*)&mp_op_contains_obj, pyobj, pykey) == mp_const_true;
    nlr_pop();
    return result;
  } else {
    record_traceback(nlr.ret_val);
    return -1;
  }
}

EMSCRIPTEN_KEEPALIVE JsRef
_pyproxy_ownKeys(mp_obj_t pyobj)
{
  mp_obj_list_t * pydir = NULL;
  JsRef iddir = NULL;
  JsRef identry = NULL;

  pydir = (mp_obj_list_t*)mp_call_function_1((mp_obj_t*)&mp_builtin_dir_obj, pyobj);

  iddir = JsArray_New();
  for (int i = 0; i < pydir->len; ++i) {
    mp_obj_t pyentry = pydir->items[i]; /* borrowed */
    identry = python2js(pyentry);
    (void)JsArray_Push(iddir, identry);
    hiwire_CLEAR(identry);
  }
  return iddir;
}

/**
 * This sets up a call to _PyObject_Vectorcall. It's a helper function for
 * callPyObjectKwargs. This is the primary entrypoint from JavaScript into
 * Python code.
 *
 * Vectorcall expects the arguments to be communicated as:
 *
 *  mp_obj_tconst *args: the positional arguments and followed by the keyword
 *    arguments
 *
 *  size_t nargs_with_flag : the number of arguments plus a flag
 *      PY_VECTORCALL_ARGUMENTS_OFFSET. The flag PY_VECTORCALL_ARGUMENTS_OFFSET
 *      indicates that we left an initial entry in the array to be used as a
 *      self argument in case the callee is a bound method.
 *
 *  mp_obj_t kwnames : a tuple of the keyword argument names. The length of
 *      this tuple tells CPython how many key word arguments there are.
 *
 * Our arguments are:
 *
 *   callable : The object to call.
 *   args : The list of JavaScript arguments, both positional and kwargs.
 *   numposargs : The number of positional arguments.
 *   kwnames : List of names of the keyword arguments
 *   numkwargs : The length of kwargs
 *
 *   Returns: The return value translated to JavaScript.
 */
EMSCRIPTEN_KEEPALIVE JsRef
_pyproxy_apply(mp_obj_t callable,
               size_t numposargs,
               size_t numkwargs,
               JsRef jsargs)
{
  JsRef jsitem = NULL;
  nlr_buf_t nlr;
  if (nlr_push(&nlr) == 0) {
    size_t total_args = numposargs + 2*numkwargs;
    mp_obj_t pyargs[total_args];
    // Put both arguments and keyword arguments into pyargs
    for (int i = 0; i < total_args; ++i) {
      jsitem = JsArray_Get(jsargs, i);
      // pyitem is moved into pyargs so we don't need to clear it later.
      mp_obj_t pyitem = js2python(jsitem);
      pyargs[i] = pyitem;
      hiwire_CLEAR(jsitem);
    }
    mp_obj_t pyresult = mp_call_function_n_kw(callable, numposargs, numkwargs, pyargs);
    JsRef idresult = python2js(pyresult);
    nlr_pop();
    return idresult;
  } else {
    hiwire_CLEAR(jsitem);
    record_traceback(nlr.ret_val);
    return NULL;
  }
}

// JsRef
// _pyproxy_iter_next(mp_obj_t iterator)
// {
//   mp_obj_t item = PyIter_Next(iterator);
//   if (item == NULL) {
//     return NULL;
//   }
//   JsRef result = python2js(item);
//   Py_CLEAR(item);
//   return result;
// }

// PySendResult
// _pyproxyGen_Send(mp_obj_t receiver, JsRef jsval, JsRef* result)
// {
//   bool success = false;
//   mp_obj_t v = NULL;
//   mp_obj_t retval = NULL;

//   v = js2python(jsval);
//   FAIL_IF_NULL(v);
//   PySendResult status = PyIter_Send(receiver, v, &retval);
//   if (status == PYGEN_ERROR) {
//     FAIL();
//   }
//   *result = python2js(retval);
//   FAIL_IF_NULL(*result);

//   success = true;
// finally:
//   Py_CLEAR(v);
//   Py_CLEAR(retval);
//   if (!success) {
//     status = PYGEN_ERROR;
//   }
//   return status;
// }

// PySendResult
// _pyproxyGen_return(mp_obj_t receiver, JsRef jsval, JsRef* result)
// {
//   bool success = false;
//   PySendResult status = PYGEN_ERROR;
//   mp_obj_t pyresult;

//   // Throw GeneratorExit into generator
//   pyresult =
//     _PyObject_CallMethodIdOneArg(receiver, &PyId_throw, PyExc_GeneratorExit);
//   if (pyresult == NULL) {
//     if (PyErr_ExceptionMatches(PyExc_GeneratorExit)) {
//       // If GeneratorExit comes back out, return original value.
//       PyErr_Clear();
//       status = PYGEN_RETURN;
//       hiwire_incref(jsval);
//       *result = jsval;
//       success = true;
//       goto finally;
//     }
//     //
//     FAIL_IF_MINUS_ONE(_PyGen_FetchStopIterationValue(&pyresult));
//     status = PYGEN_RETURN;
//   } else {
//     status = PYGEN_NEXT;
//   }
//   *result = python2js(pyresult);
//   FAIL_IF_NULL(*result);
//   success = true;
// finally:
//   if (!success) {
//     status = PYGEN_ERROR;
//   }
//   Py_CLEAR(pyresult);
//   return status;
// }

// PySendResult
// _pyproxyGen_throw(mp_obj_t receiver, JsRef jsval, JsRef* result)
// {
//   bool success = false;
//   mp_obj_t pyvalue = NULL;
//   mp_obj_t pyresult = NULL;
//   PySendResult status = PYGEN_ERROR;

//   pyvalue = js2python(jsval);
//   FAIL_IF_NULL(pyvalue);
//   if (!PyExceptionInstance_Check(pyvalue)) {
//     /* Not something you can raise.  throw() fails. */
//     PyErr_Format(PyExc_TypeError,
//                  "exceptions must be classes or instances "
//                  "deriving from BaseException, not %s",
//                  Py_TYPE(pyvalue)->tp_name);
//     FAIL();
//   }
//   pyresult = _PyObject_CallMethodIdOneArg(receiver, &PyId_throw, pyvalue);
//   if (pyresult == NULL) {
//     FAIL_IF_MINUS_ONE(_PyGen_FetchStopIterationValue(&pyresult));
//     status = PYGEN_RETURN;
//   } else {
//     status = PYGEN_NEXT;
//   }
//   *result = python2js(pyresult);
//   FAIL_IF_NULL(*result);
//   success = true;
// finally:
//   if (!success) {
//     status = PYGEN_ERROR;
//   }
//   Py_CLEAR(pyresult);
//   Py_CLEAR(pyvalue);
//   return status;
// }

// JsRef
// _pyproxyGen_asend(mp_obj_t receiver, JsRef jsval)
// {
//   mp_obj_t v = NULL;
//   mp_obj_t asend = NULL;
//   mp_obj_t pyresult = NULL;
//   JsRef jsresult = NULL;

//   v = js2python(jsval);
//   FAIL_IF_NULL(v);
//   asend = _PyObject_GetAttrId(receiver, &PyId_asend);
//   if (asend) {
//     pyresult = PyObject_CallOneArg(asend, v);
//   } else {
//     PyErr_Clear();
//     PyTypeObject* t = Py_TYPE(receiver);
//     if (t->tp_as_async == NULL || t->tp_as_async->am_anext == NULL) {
//       PyErr_Format(PyExc_TypeError,
//                    "'%.200s' object is not an async iterator",
//                    t->tp_name);
//       return NULL;
//     }
//     pyresult = (*t->tp_as_async->am_anext)(receiver);
//   }
//   FAIL_IF_NULL(pyresult);

//   jsresult = python2js(pyresult);
//   FAIL_IF_NULL(jsresult);

// finally:
//   Py_CLEAR(v);
//   Py_CLEAR(asend);
//   Py_CLEAR(pyresult);
//   return jsresult;
// }

// JsRef
// _pyproxyGen_areturn(mp_obj_t receiver)
// {
//   mp_obj_t v = NULL;
//   mp_obj_t asend = NULL;
//   mp_obj_t pyresult = NULL;
//   JsRef jsresult = NULL;

//   pyresult =
//     _PyObject_CallMethodIdOneArg(receiver, &PyId_athrow, PyExc_GeneratorExit);
//   FAIL_IF_NULL(pyresult);

//   jsresult = python2js(pyresult);
//   FAIL_IF_NULL(jsresult);

// finally:
//   Py_CLEAR(v);
//   Py_CLEAR(asend);
//   Py_CLEAR(pyresult);
//   return jsresult;
// }

// JsRef
// _pyproxyGen_athrow(mp_obj_t receiver, JsRef jsval)
// {
//   mp_obj_t v = NULL;
//   mp_obj_t asend = NULL;
//   mp_obj_t pyresult = NULL;
//   JsRef jsresult = NULL;

//   v = js2python(jsval);
//   FAIL_IF_NULL(v);
//   if (!PyExceptionInstance_Check(v)) {
//     /* Not something you can raise.  throw() fails. */
//     PyErr_Format(PyExc_TypeError,
//                  "exceptions must be classes or instances "
//                  "deriving from BaseException, not %s",
//                  Py_TYPE(v)->tp_name);
//     FAIL();
//   }
//   pyresult = _PyObject_CallMethodIdOneArg(receiver, &PyId_athrow, v);
//   FAIL_IF_NULL(pyresult);

//   jsresult = python2js(pyresult);
//   FAIL_IF_NULL(jsresult);

// finally:
//   Py_CLEAR(v);
//   Py_CLEAR(asend);
//   Py_CLEAR(pyresult);
//   return jsresult;
// }

// JsRef
// _pyproxy_aiter_next(mp_obj_t aiterator)
// {
//   PyTypeObject* t;
//   mp_obj_t awaitable;

//   t = Py_TYPE(aiterator);
//   if (t->tp_as_async == NULL || t->tp_as_async->am_anext == NULL) {
//     PyErr_Format(
//       PyExc_TypeError, "'%.200s' object is not an async iterator", t->tp_name);
//     return NULL;
//   }

//   awaitable = (*t->tp_as_async->am_anext)(aiterator);
//   if (awaitable == NULL) {
//     return NULL;
//   }
//   JsRef result = python2js(awaitable);
//   Py_CLEAR(awaitable);
//   return result;
// }

// ///////////////////////////////////////////////////////////////////////////////
// //
// // Await / "then" Implementation
// //
// // We want convert the object to a future with `ensure_future` and then make a
// // promise that resolves when the future does. We can add a callback to the
// // future with future.add_done_callback but we need to make a little python
// // closure "FutureDoneCallback" that remembers how to resolve the promise.
// //
// // From JavaScript we will use the single function _pyproxy_ensure_future, the
// // rest of this segment is helper functions for _pyproxy_ensure_future. The
// // FutureDoneCallback object is never exposed to the user.

// /**
//  * A simple Callable python object. Intended to be called with a single argument
//  * which is the future that was resolved.
//  */
// // clang-format off
// typedef struct {
//     PyObject_HEAD
//     /** Will call this function with the result if the future succeeded */
//     JsRef resolve_handle;
//     /** Will call this function with the error if the future succeeded */
//     JsRef reject_handle;
// } FutureDoneCallback;
// // clang-format on

// static void
// FutureDoneCallback_dealloc(FutureDoneCallback* self)
// {
//   hiwire_CLEAR(self->resolve_handle);
//   hiwire_CLEAR(self->reject_handle);
//   Py_TYPE(self)->tp_free((mp_obj_t)self);
// }

// /**
//  * Helper method: if the future resolved successfully, call resolve_handle on
//  * the result.
//  */
// int
// FutureDoneCallback_call_resolve(FutureDoneCallback* self, mp_obj_t result)
// {
//   JsRef result_js = NULL;
//   JsRef output = NULL;
//   result_js = python2js(result);
//   output = hiwire_call_OneArg(self->resolve_handle, result_js);

//   hiwire_CLEAR(result_js);
//   hiwire_CLEAR(output);
//   return 0;
// }

// /**
//  * Helper method: if the future threw an error, call reject_handle on a
//  * converted exception. The caller leaves the python error indicator set.
//  */
// int
// FutureDoneCallback_call_reject(FutureDoneCallback* self)
// {
//   bool success = false;
//   JsRef excval = NULL;
//   JsRef result = NULL;
//   // wrap_exception looks up the current exception and wraps it in a Js error.
//   excval = wrap_exception();
//   FAIL_IF_NULL(excval);
//   result = hiwire_call_OneArg(self->reject_handle, excval);

//   success = true;
// finally:
//   hiwire_CLEAR(excval);
//   hiwire_CLEAR(result);
//   return success ? 0 : -1;
// }

// /**
//  * Intended to be called with a single argument which is the future that was
//  * resolved. Resolves the promise as appropriate based on the result of the
//  * future.
//  */
// mp_obj_t
// FutureDoneCallback_call(FutureDoneCallback* self,
//                         mp_obj_t args,
//                         mp_obj_t kwargs)
// {
//   mp_obj_t fut;
//   if (!PyArg_UnpackTuple(args, "future_done_callback", 1, 1, &fut)) {
//     return NULL;
//   }
//   mp_obj_t result = _PyObject_CallMethodIdNoArgs(fut, &PyId_result);
//   int errcode;
//   if (result != NULL) {
//     errcode = FutureDoneCallback_call_resolve(self, result);
//     Py_DECREF(result);
//   } else {
//     errcode = FutureDoneCallback_call_reject(self);
//   }
//   if (errcode == 0) {
//     Py_RETURN_NONE;
//   } else {
//     return NULL;
//   }
// }

// // clang-format off
// static PyTypeObject FutureDoneCallbackType = {
//     .tp_name = "FutureDoneCallback",
//     .tp_doc = "Callback for internal use to allow awaiting a future from javascript",
//     .tp_basicsize = sizeof(FutureDoneCallback),
//     .tp_itemsize = 0,
//     .tp_flags = Py_TPFLAGS_DEFAULT,
//     .tp_dealloc = (destructor) FutureDoneCallback_dealloc,
//     .tp_call = (ternaryfunc) FutureDoneCallback_call,
// };
// // clang-format on

// static mp_obj_t
// FutureDoneCallback_cnew(JsRef resolve_handle, JsRef reject_handle)
// {
//   FutureDoneCallback* self =
//     (FutureDoneCallback*)FutureDoneCallbackType.tp_alloc(
//       &FutureDoneCallbackType, 0);
//   self->resolve_handle = hiwire_incref(resolve_handle);
//   self->reject_handle = hiwire_incref(reject_handle);
//   return (mp_obj_t)self;
// }

// /**
//  * Intended to be called with a single argument which is the future that was
//  * resolved. Resolves the promise as appropriate based on the result of the
//  * future.
//  *
//  * :param pyobject: An awaitable python object
//  * :param resolve_handle: The resolve javascript method for a promise
//  * :param reject_handle: The reject javascript method for a promise
//  * :return: 0 on success, -1 on failure
//  */
// int
// _pyproxy_ensure_future(mp_obj_t pyobject,
//                        JsRef resolve_handle,
//                        JsRef reject_handle)
// {
//   bool success = false;
//   mp_obj_t future = NULL;
//   mp_obj_t callback = NULL;
//   mp_obj_t retval = NULL;
//   future = _PyObject_CallMethodIdOneArg(asyncio, &PyId_ensure_future, pyobject);
//   FAIL_IF_NULL(future);
//   callback = FutureDoneCallback_cnew(resolve_handle, reject_handle);
//   retval =
//     _PyObject_CallMethodIdOneArg(future, &PyId_add_done_callback, callback);
//   FAIL_IF_NULL(retval);

//   success = true;
// finally:
//   Py_CLEAR(future);
//   Py_CLEAR(callback);
//   Py_CLEAR(retval);
//   return success ? 0 : -1;
// }

// ///////////////////////////////////////////////////////////////////////////////
// //
// // Buffers
// //

// // For debug
// size_t py_buffer_len_offset = offsetof(Py_buffer, len);
// size_t py_buffer_shape_offset = offsetof(Py_buffer, shape);

// /**
//  * Convert a C array of Py_ssize_t to JavaScript.
//  */
// EM_JS(JsRef, array_to_js, (Py_ssize_t * array, int len), {
//   return Hiwire.new_value(
//     Array.from(HEAP32.subarray(array / 4, array / 4 + len)));
// })

// // The order of these fields has to match the code in getBuffer
// typedef struct
// {
//   // where is the first entry buffer[0]...[0] (ndim times)?
//   void* start_ptr;
//   // Where is the earliest location in buffer? (If all strides are positive,
//   // this equals start_ptr)
//   void* smallest_ptr;
//   // What is the last location in the buffer (plus one)
//   void* largest_ptr;

//   int readonly;
//   char* format;
//   int itemsize;
//   JsRef shape;
//   JsRef strides;

//   Py_buffer* view;
//   int c_contiguous;
//   int f_contiguous;
// } buffer_struct;

// size_t buffer_struct_size = sizeof(buffer_struct);

// /**
//  * This is the C part of the getBuffer method.
//  *
//  * We use PyObject_GetBuffer to acquire a Py_buffer view to the object, then we
//  * determine the locations of: the first element of the buffer, the earliest
//  * element of the buffer in memory the latest element of the buffer in memory
//  * (plus one itemsize).
//  *
//  * We will use this information to slice out a subarray of the wasm heap that
//  * contains all the memory inside of the buffer.
//  *
//  * Special care must be taken for negative strides, this is why we need to keep
//  * track separately of start_ptr (the location of the first element of the
//  * array) and smallest_ptr (the location of the earliest element of the array in
//  * memory). If strides are positive, these are the same but if some strides are
//  * negative they will be different.
//  *
//  * We also put the various other metadata about the buffer that we want to share
//  * into buffer_struct.
//  */
// int
// _pyproxy_get_buffer(buffer_struct* target, mp_obj_t ptrobj)
// {
//   Py_buffer view;
//   // PyBUF_RECORDS_RO requires that suboffsets be NULL but otherwise is the most
//   // permissive possible request.
//   if (PyObject_GetBuffer(ptrobj, &view, PyBUF_RECORDS_RO) == -1) {
//     // Buffer cannot be represented without suboffsets. The bf_getbuffer method
//     // should have set a PyExc_BufferError saying something to this effect.
//     return -1;
//   }

//   buffer_struct result = { 0 };
//   result.start_ptr = result.smallest_ptr = result.largest_ptr = view.buf;
//   result.readonly = view.readonly;

//   result.format = view.format;
//   result.itemsize = view.itemsize;

//   if (view.ndim == 0) {
//     // "If ndim is 0, buf points to a single item representing a scalar. In this
//     // case, shape, strides and suboffsets MUST be NULL."
//     // https://docs.python.org/3/c-api/buffer.html#c.Py_buffer.ndim
//     result.largest_ptr += view.itemsize;
//     result.shape = JsArray_New();
//     result.strides = JsArray_New();
//     result.c_contiguous = true;
//     result.f_contiguous = true;
//     goto success;
//   }

//   // Because we requested PyBUF_RECORDS_RO I think we can assume that
//   // view.shape != NULL.
//   result.shape = array_to_js(view.shape, view.ndim);

//   if (view.strides == NULL) {
//     // In this case we are a C contiguous buffer
//     result.largest_ptr += view.len;
//     Py_ssize_t strides[view.ndim];
//     PyBuffer_FillContiguousStrides(
//       view.ndim, view.shape, strides, view.itemsize, 'C');
//     result.strides = array_to_js(strides, view.ndim);
//     goto success;
//   }

//   if (view.len != 0) {
//     // Have to be careful to ensure that we handle negative strides correctly.
//     for (int i = 0; i < view.ndim; i++) {
//       // view.strides[i] != 0
//       if (view.strides[i] > 0) {
//         // add positive strides to largest_ptr
//         result.largest_ptr += view.strides[i] * (view.shape[i] - 1);
//       } else {
//         // subtract negative strides from smallest_ptr
//         result.smallest_ptr += view.strides[i] * (view.shape[i] - 1);
//       }
//     }
//     result.largest_ptr += view.itemsize;
//   }

//   result.strides = array_to_js(view.strides, view.ndim);
//   result.c_contiguous = PyBuffer_IsContiguous(&view, 'C');
//   result.f_contiguous = PyBuffer_IsContiguous(&view, 'F');

// success:
//   // The result.view memory will be freed when (if?) the user calls
//   // Py_Buffer.release().
//   result.view = (Py_buffer*)PyMem_Malloc(sizeof(Py_buffer));
//   *result.view = view;
//   *target = result;
//   return 0;
// }

// EM_JS_REF(JsRef,
//           pyproxy_new_ex,
//           (PyObject * ptrobj, bool capture_this, bool roundtrip),
//           {
//             return Hiwire.new_value(Module.pyproxy_new(ptrobj, {
//               props : { captureThis : !!capture_this, roundtrip : !!roundtrip }
//             }));
//           });

EM_JS_REF(JsRef, pyproxy_new_js, (mp_obj_t * ptrobj), {
  return Hiwire.new_value(Module.pyproxy_new(ptrobj));
});

JsRef
pyproxy_new(mp_obj_t obj) {
  nlr_buf_t nlr;
  if (nlr_push(&nlr) == 0) {
    mp_int_t refcnt = 0;
    mp_map_t *proxy_dict_map = mp_obj_dict_get_map(proxy_dict);
    mp_obj_t objkey = mp_obj_new_int((size_t)obj);
    mp_map_elem_t *elem = mp_map_lookup(proxy_dict_map, objkey, MP_MAP_LOOKUP);
    if(elem != NULL){
      size_t len; 
      mp_obj_t *items;
      mp_obj_tuple_get(elem->value, &len, &items);
      refcnt = MP_OBJ_SMALL_INT_VALUE(items[1]);
    }
    refcnt ++;
    mp_obj_t tuple[2];
    tuple[0] = obj;
    tuple[1] = mp_obj_new_int(refcnt);
    mp_obj_t val = mp_obj_new_tuple(2, tuple);
    mp_obj_dict_store(proxy_dict, objkey, val);
    JsRef result = pyproxy_new_js(obj);
    nlr_pop();
    return result;
  } else {
    printf("Internal error in pyproxy_new:\n");
    mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
    return NULL;
  }
}

EMSCRIPTEN_KEEPALIVE void
pyproxy_decref(mp_obj_t obj) {
  nlr_buf_t nlr;
  if (nlr_push(&nlr) == 0) {
    mp_map_t *proxy_dict_map = mp_obj_dict_get_map(proxy_dict);
    mp_obj_t objkey = mp_obj_new_int((size_t)obj);
    mp_map_elem_t *elem = mp_map_lookup(proxy_dict_map, objkey, MP_MAP_LOOKUP);
    if(elem == NULL){
      // Internal error...
      printf("Internal error...");
      return;
    }
    mp_int_t refcnt = MP_OBJ_SMALL_INT_VALUE(elem->value);
    refcnt --;
    if(refcnt == 0) {
      mp_obj_dict_delete(objkey, obj);
      return;
    }
    mp_obj_t tuple[2];
    tuple[0] = obj;
    tuple[1] = mp_obj_new_int(refcnt);
    mp_obj_t val = mp_obj_new_int(refcnt);
    mp_obj_dict_store(proxy_dict, objkey, val);
    nlr_pop();
  } else {
    printf("Internal error in pyproxy_decref:\n");
    mp_obj_print_exception(&mp_plat_print, MP_OBJ_FROM_PTR(nlr.ret_val));
  }
}

// /**
//  * Create a JsRef which can be called once, wrapping a Python callable. The
//  * JsRef owns a reference to the Python callable until it is called, then
//  * releases it. Useful for the "finally" wrapper on a JsProxy of a promise, and
//  * also exposed in the pyodide Python module.
//  */
// EM_JS_REF(JsRef, create_once_callable, (PyObject * obj), {
//   _Py_IncRef(obj);
//   let alreadyCalled = false;
//   function wrapper(... args)
//   {
//     if (alreadyCalled) {
//       throw new Error("OnceProxy can only be called once");
//     }
//     try {
//       return Module.callPyObject(obj, args);
//     } finally {
//       wrapper.destroy();
//     }
//   }
//   wrapper.destroy = function()
//   {
//     if (alreadyCalled) {
//       throw new Error("OnceProxy has already been destroyed");
//     }
//     alreadyCalled = true;
//     Module.finalizationRegistry.unregister(wrapper);
//     _Py_DecRef(obj);
//   };
//   Module.finalizationRegistry.register(wrapper, [ obj, undefined ], wrapper);
//   return Hiwire.new_value(wrapper);
// });

// static mp_obj_t
// create_once_callable_py(mp_obj_t _mod, mp_obj_t obj)
// {
//   JsRef ref = create_once_callable(obj);
//   mp_obj_t result = JsProxy_create(ref);
//   hiwire_decref(ref);
//   return result;
// }

// // clang-format off

// /**
//  * Arguments:
//  *  handle_result -- Python callable expecting one argument, called with the
//  *  result if the promise is resolved. Can be NULL.
//  *
//  *  handle_exception -- Python callable expecting one argument, called with the
//  *  exception if the promise is rejected. Can be NULL.
//  *
//  *  done_callback_id -- A JsRef to a JavaScript callback to be called when the
//  *  promise is either resolved or rejected. Can be NULL.
//  *
//  * Returns: a JsRef to a pair [onResolved, onRejected].
//  *
//  * The purpose of this function is to handle memory management when attaching
//  * Python functions to Promises. This function stores a reference to both
//  * handle_result and handle_exception, and frees both when either onResolved or
//  * onRejected is called. Of course if the Promise never resolves then the
//  * handles will be leaked. We can't use create_once_callable because either
//  * onResolved or onRejected is called but not both. In either case, we release
//  * both functions.
//  *
//  * The return values are intended to be attached to a promise e.g.,
//  * some_promise.then(onResolved, onRejected).
//  */
// EM_JS_REF(JsRef, create_promise_handles, (
//   mp_obj_t handle_result, mp_obj_t handle_exception, JsRef done_callback_id
// ), {
//   // At some point it would be nice to use FinalizationRegistry with these, but
//   // it's a bit tricky.
//   if (handle_result) {
//     _Py_IncRef(handle_result);
//   }
//   if (handle_exception) {
//     _Py_IncRef(handle_exception);
//   }
//   let done_callback = (x) => {};
//   if(done_callback_id){
//     done_callback = Hiwire.get_value(done_callback_id);
//   }
//   let used = false;
//   function checkUsed(){
//     if (used) {
//       throw new Error("One of the promise handles has already been called.");
//     }
//   }
//   function destroy(){
//     checkUsed();
//     used = true;
//     if(handle_result){
//       _Py_DecRef(handle_result);
//     }
//     if(handle_exception){
//       _Py_DecRef(handle_exception)
//     }
//   }
//   function onFulfilled(res) {
//     checkUsed();
//     try {
//       if(handle_result){
//         return Module.callPyObject(handle_result, [res]);
//       }
//     } finally {
//       done_callback(res);
//       destroy();
//     }
//   }
//   function onRejected(err) {
//     checkUsed();
//     try {
//       if(handle_exception){
//         return Module.callPyObject(handle_exception, [err]);
//       }
//     } finally {
//       done_callback(undefined);
//       destroy();
//     }
//   }
//   onFulfilled.destroy = destroy;
//   onRejected.destroy = destroy;
//   return Hiwire.new_value(
//     [onFulfilled, onRejected]
//   );
// })
// // clang-format on

// static mp_obj_t
// create_proxy(mp_obj_t self,
//              mp_obj_t const* args,
//              Py_ssize_t nargs,
//              mp_obj_t kwnames)
// {
//   static const char* const _keywords[] = { "", "capture_this", "roundtrip", 0 };
//   bool capture_this = false;
//   bool roundtrip = true;
//   mp_obj_t obj;
//   static struct _PyArg_Parser _parser = { "O|$pp:create_proxy", _keywords, 0 };
//   if (!_PyArg_ParseStackAndKeywords(
//         args, nargs, kwnames, &_parser, &obj, &capture_this, &roundtrip)) {
//     return NULL;
//   }

//   JsRef ref = pyproxy_new_ex(obj, capture_this, roundtrip);
//   mp_obj_t result = JsProxy_create(ref);
//   hiwire_decref(ref);
//   return result;
// }

// static PyMethodDef methods[] = {
//   {
//     "create_once_callable",
//     create_once_callable_py,
//     METH_O,
//   },
//   {
//     "create_proxy",
//     (PyCFunction)create_proxy,
//     METH_FASTCALL | METH_KEYWORDS,
//   },
//   { NULL } /* Sentinel */
// };

// int
// pyproxy_init(mp_obj_t core)
// {
//   bool success = false;

//   mp_obj_t collections_abc = NULL;
//   mp_obj_t docstring_source = NULL;

//   collections_abc = PyImport_ImportModule("collections.abc");
//   FAIL_IF_NULL(collections_abc);
//   Generator = PyObject_GetAttrString(collections_abc, "Generator");
//   FAIL_IF_NULL(Generator);
//   AsyncGenerator = PyObject_GetAttrString(collections_abc, "AsyncGenerator");
//   FAIL_IF_NULL(AsyncGenerator);

//   docstring_source = PyImport_ImportModule("_pyodide._core_docs");
//   FAIL_IF_NULL(docstring_source);
//   FAIL_IF_MINUS_ONE(
//     add_methods_and_set_docstrings(core, methods, docstring_source));
//   asyncio = PyImport_ImportModule("asyncio");
//   FAIL_IF_NULL(asyncio);
//   FAIL_IF_MINUS_ONE(PyType_Ready(&FutureDoneCallbackType));

//   success = true;
// finally:
//   Py_CLEAR(docstring_source);
//   Py_CLEAR(collections_abc);
//   return success ? 0 : -1;
// }


#include "include_js_file.h"
#include "pyproxy.js"


int pyproxy_init(void) {
  proxy_dict = mp_obj_new_dict(0);
  return pyproxy_init_js();
}
