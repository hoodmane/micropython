#include "jsproxy.h"

#include "py/obj.h"
#include "py/objmodule.h"
#include "py/objstr.h"
#include "py/runtime.h"

#include "hiwire.h"
#include "js2python.h"
#include "jsmemops.h"
#include "pyproxy.h"
#include "python2js.h"

#define MAYBE_RERAISE(nlr)                                                     \
  do {                                                                         \
    if (nlr.ret_val) {                                                         \
      nlr_raise(MP_OBJ_FROM_PTR(nlr.ret_val));                                 \
    }                                                                          \
  } while (0)

typedef struct _mp_obj_jsobj_t
{
  mp_obj_base_t base;
  JsRef ref;
  JsRef this;
} JsProxy;

STATIC void
JsProxy_print(const mp_print_t* print, mp_obj_t self_in, mp_print_kind_t kind)
{
  JsProxy* self = MP_OBJ_TO_PTR(self_in);
  JsRef jsstr = hiwire_to_string(self->ref);
  mp_obj_t pystr = js2python(jsstr);
  if (kind == PRINT_REPR) {
    mp_printf(
      print, "<JsProxy %d: %s>", (int)self->ref, mp_obj_str_get_str(pystr));
  } else {
    mp_printf(print, "%s", mp_obj_str_get_str(pystr));
  }
}

STATIC JsRef
JsMethod_ConvertArgs(size_t n_args,
                     size_t n_kw,
                     const mp_obj_t* args,
                     JsRef proxies)
{
  JsRef idargs = JsArray_New();
  JsRef idkwargs = NULL;
  JsRef jsvalue = NULL;
  nlr_buf_t nlr = { 0 };
  if (nlr_push(&nlr) == 0) {
    for (int i = 0; i < n_args; ++i) {
      JsRef idarg = python2js_track_proxies(args[i], proxies);
      JsArray_Push(idargs, idarg);
      hiwire_CLEAR(idarg);
    }

    if (n_kw == 0) {
      goto nokwargs;
    }
    const mp_obj_t* kwargs = args + n_args;

    idkwargs = JsObject_New();
    for (int i = 0; i < n_kw; ++i) {
      // store kwargs into an object which we'll use as the last argument.
      const mp_obj_t pyname = kwargs[2 * i];
      const mp_obj_t pyvalue = kwargs[2 * i + 1];
      GET_STR_DATA_LEN(pyname, name_utf8, len);
      jsvalue = python2js_track_proxies(pyvalue, proxies);
      JsObject_SetString(idkwargs, (const char*)name_utf8, jsvalue);
      hiwire_CLEAR(jsvalue);
    }

    JsArray_Push(idargs, idkwargs);
  nokwargs:
    hiwire_CLEAR(idkwargs);
    nlr_pop();
  } else {
    hiwire_CLEAR(idargs);
  }
  hiwire_CLEAR(idkwargs);
  hiwire_CLEAR(jsvalue);
  MAYBE_RERAISE(nlr);
  return idargs;
}


static char* PYPROXY_DESTROYED_AT_END_OF_FUNCTION_CALL =
  "This borrowed proxy was automatically destroyed at the "
  "end of a function call. Try using "
  "create_proxy or create_once_callable.";

STATIC mp_obj_t
JsMethod_call(mp_obj_t self_in,
              size_t n_args,
              size_t n_kw,
              const mp_obj_t* args)
{
  JsProxy* self = MP_OBJ_TO_PTR(self_in);

  JsRef proxies = NULL;
  JsRef idargs = NULL;
  JsRef idresult = NULL;
  mp_obj_t result = NULL;

  nlr_buf_t nlr = { 0 };
  if (nlr_push(&nlr) == 0) {
    proxies = JsArray_New();
    idargs = JsMethod_ConvertArgs(n_args, n_kw, args, proxies);
    idresult = hiwire_call_bound(self->ref, self->this, idargs);
    result = js2python(idresult);
    nlr_pop();
  }

  if (proxies != NULL) {
    destroy_proxies(proxies, PYPROXY_DESTROYED_AT_END_OF_FUNCTION_CALL);
  }
  hiwire_CLEAR(proxies);
  hiwire_CLEAR(idargs);
  hiwire_CLEAR(idresult);
  MAYBE_RERAISE(nlr);
  return result;
}

// static inline JsRef JsProxy_get_ref(mp_obj_t o) {
//     JsProxy *self = MP_OBJ_TO_PTR(o);
//     return self->ref;
// }

STATIC void
JsProxy_attr(mp_obj_t self_in, qstr attr, mp_obj_t* dest)
{
  JsProxy* self = MP_OBJ_TO_PTR(self_in);
  JsRef jsvalue = NULL;
  if (dest[0] == MP_OBJ_NULL) {
    // Load
    if (attr == MP_QSTR_new) {
      // Special case to handle construction of JS objects.
      return;
    }
    if (attr == MP_QSTR_js_id) {
      dest[0] = mp_obj_new_int_from_uint((mp_uint_t)(self->ref));
      return;
    }
    jsvalue = JsObject_GetString(self->ref, qstr_str(attr));
    if (jsvalue == NULL) {
      mp_raise_msg_varg(&mp_type_AttributeError,
                        MP_ERROR_TEXT("'%s' object has no attribute '%q'"),
                        mp_obj_get_type_str(self_in),
                        attr);
    }
    mp_obj_t pyresult;
    if (!pyproxy_Check(jsvalue) && hiwire_is_function(jsvalue)) {
      pyresult = JsProxy_new_with_this(jsvalue, self->ref);
    } else {
      pyresult = js2python(jsvalue);
    }
    dest[0] = pyresult;
  } else if (dest[1] == MP_OBJ_NULL) {
    // Delete
    JsObject_DeleteString(self->ref, qstr_str(attr));
    dest[0] = MP_OBJ_NULL;
  } else {
    // Store
    jsvalue = python2js(dest[1]);
    JsObject_SetString(self->ref, qstr_str(attr), jsvalue);
    dest[0] = MP_OBJ_NULL;
  }
  hiwire_CLEAR(jsvalue);
}

typedef struct _jsobj_it_t
{
  mp_obj_base_t base;
  mp_fun_1_t iternext;
  JsRef ref;
} JsProxy_it_t;

EM_JS_REF(JsRef, JsProxy_GetIter_js, (JsRef idobj), {
  let jsobj = Hiwire.get_value(idobj);
  return Hiwire.new_value(jsobj[Symbol.iterator]());
});

// clang-format off
EM_JS_NUM(
void,
JsProxy_iternext_js,
(JsRef idit, bool* done_ptr, JsRef* value_ptr),
{
  let it = Hiwire.get_value(idit);
  let { done, value } = it.next();
  DEREF_U8(done_ptr, 0) = done;
  DEREF_U32(value_ptr, 0) = Hiwire.new_value(value);
})
// clang-format on

STATIC mp_obj_t
JsProxy_it_iternext(mp_obj_t self_in)
{
  JsProxy_it_t* self = MP_OBJ_TO_PTR(self_in);
  bool done;
  JsRef value;
  JsProxy_iternext_js(self->ref, &done, &value);
  mp_obj_t result;
  if (done) {
    result = MP_OBJ_STOP_ITERATION;
  } else {
    result = js2python(value);
  }
  hiwire_CLEAR(value);
  return result;
}

STATIC mp_obj_t
JsProxy_GetIter(mp_obj_t o_in, mp_obj_iter_buf_t* iter_buf)
{
  JsProxy* self = MP_OBJ_TO_PTR(o_in);
  assert(sizeof(JsProxy_it_t) <= sizeof(mp_obj_iter_buf_t));
  JsProxy_it_t* o = (JsProxy_it_t*)iter_buf;
  o->base.type = &mp_type_polymorph_iter;
  o->iternext = JsProxy_it_iternext;
  o->ref = JsProxy_GetIter_js(self->ref);
  return MP_OBJ_FROM_PTR(o);
}

#define SUBSCR_LOAD 0
#define SUBSCR_STORE 1
#define SUBSCR_DELETE 2

#define SUBSCR_KEY_ERROR 1
#define SUBSCR_INDEX_ERROR 2
#define SUBSCR_TYPE_ERROR 3

// clang-format off
EM_JS_REF(
JsRef,
JsProxy_subscr_js,
(JsRef idself, JsRef idkey, JsRef idvalue, int type, int* error),
{
  const obj = Hiwire.get_value(idself);
  const isArray = Array.isArray(obj);
  const isTypedArray =
    ArrayBuffer.isView(obj) && obj.constructor.name !== "DataView";
  const typeTag = getTypeTag(obj);
  // I think there are also CSS collections and other DomArrays but
  // whatever
  const isDomArray =
    typeTag === "[object HTMLCollection]" || typeTag === "[object NodeList]";

  const array_like = isArray || isTypedArray || isDomArray;
  const map_like = !isArray && "get" in obj;
  if (!array_like && !map_like) {
    DEREF_U32(error, 0) = SUBSCR_TYPE_ERROR;
    return NULL;
  }

  let key = Hiwire.get_value(idkey);
  if (array_like && typeof key === "number" && key < 0) {
    key = obj.length + key;
  }
  if (type === SUBSCR_LOAD) {
    let result;
    if (map_like) {
      result = obj.get(key);
      if (result !== undefined) {
        return Hiwire.new_value(result);
      }
      // Try to distinguish between undefined and missing:
      // If the object has a "has" method and it returns false for
      // this key, the key is missing. Otherwise, assume key present
      // and value was undefined.
      if (obj.has && typeof obj.has === "function" && obj.has(key)) {
        return DEREF_U32(_Js_undefined, 0);
      }
      // key error
      DEREF_U32(error, 0) = SUBSCR_KEY_ERROR;
      return NULL;
    }
    if (!(key in obj)) {
      // key error
      DEREF_U32(error, 0) = SUBSCR_INDEX_ERROR;
      return NULL;
    }
    return Hiwire.new_value(obj[key]);
  }

  if (type === SUBSCR_STORE) {
    const value = Hiwire.get_value(idvalue);
    if (map_like) {
      obj.set(key, value);
    } else {
      obj[key] = value;
    }
    return Js_undefined;
  }
  if (type === SUBSCR_DELETE) {
    if (map_like) {
      obj.delete(key);
    } else {
      delete obj[key];
    }
    return Js_undefined;
  }
})
// clang-format on

STATIC mp_obj_t
JsProxy_subscr(mp_obj_t self_in, mp_obj_t index, mp_obj_t value)
{
  JsProxy* self = MP_OBJ_TO_PTR(self_in);
  JsRef jsindex = NULL;
  JsRef jsvalue = NULL;
  JsRef jsresult = NULL;

  jsindex = python2js(index);
  int type;
  int error = 0;
  if (value == MP_OBJ_NULL) {
    type = SUBSCR_DELETE;
  } else if (value == MP_OBJ_SENTINEL) {
    // load
    type = SUBSCR_LOAD;
  } else {
    jsvalue = python2js(value);
    // store
    type = SUBSCR_STORE;
  }
  jsresult = JsProxy_subscr_js(self->ref, jsindex, jsvalue, type, &error);
  if (error == SUBSCR_KEY_ERROR) {
    mp_raise_type_arg(&mp_type_KeyError, index);
  }
  if (error == SUBSCR_INDEX_ERROR) {
    mp_raise_msg(&mp_type_IndexError, MP_ERROR_TEXT("index out of range"));
  }
  if (error == SUBSCR_TYPE_ERROR) {
    mp_raise_msg(&mp_type_TypeError,
                 MP_ERROR_TEXT("object isn't subscriptable"));
  }
  mp_obj_t pyresult = js2python(jsresult);
  hiwire_CLEAR(jsindex);
  hiwire_CLEAR(jsvalue);
  hiwire_CLEAR(jsresult);
  return pyresult;
}

// STATIC mp_obj_t JsMethod_construct(size_t n, const mp_obj_t *args, mp_map_t
// *kwargs) {
//     JsRef ref = JsProxy_get_ref(args[0]);
//     const char *arg1 = mp_obj_str_get_str(args[1]);
//     uint32_t out[3];
//     js_reflect_construct(arg0, arg1, out);
//     return convert_to_mp_obj_cside(out);
// }
// STATIC MP_DECLARE_CONST_FUN_OBJ_KW(jsobj_reflect_construct_obj, 1,
// jsobj_reflect_construct);

// STATIC const mp_rom_map_elem_t dict_locals_dict_table[] = {
//     { MP_ROM_QSTR(MP_QSTR_new), MP_ROM_PTR(&dict_items_obj) },
// };
// STATIC MP_DEFINE_CONST_DICT(dict_locals_dict, dict_locals_dict_table);

// clang-format off
STATIC MP_DEFINE_CONST_OBJ_TYPE(
  mp_type_JsProxy,
  MP_QSTR_JsProxy,
  0, //MP_TYPE_FLAG_ITER_IS_GETITER,
  print, JsProxy_print,
  call, JsMethod_call,
  attr, JsProxy_attr,
  iter, JsProxy_GetIter,
  subscr, JsProxy_subscr
);
// clang-format on

mp_obj_t
JsProxy_new_with_this(JsRef ref, JsRef this)
{
  JsProxy* o = mp_obj_malloc(JsProxy, &mp_type_JsProxy);
  o->ref = hiwire_incref(ref);
  o->this = hiwire_incref(this);
  return MP_OBJ_FROM_PTR(o);
}

EMSCRIPTEN_KEEPALIVE mp_obj_t
JsProxy_new(JsRef ref)
{
  return JsProxy_new_with_this(ref, Js_null);
}

bool
JsProxy_Check(mp_obj_t x)
{
  const mp_obj_type_t* type = mp_obj_get_type(x);

  return type == &mp_type_JsProxy;
}

JsRef
JsProxy_AsJs(mp_obj_t self_in)
{
  JsProxy* self = MP_OBJ_TO_PTR(self_in);
  return hiwire_incref(self->ref);
}

// STATIC void
// mp_module_js_attr(mp_obj_t self_in, qstr attr, mp_obj_t* dest)
// {
//   JsProxy* self = MP_OBJ_TO_PTR(self_in);

//   if (dest[0] == MP_OBJ_NULL) {
//     return JsObject_GetString(self->ref, qstr_str(attr))
//   }

// }

// STATIC const mp_rom_map_elem_t mp_module_js_globals_table[] = {
//   { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_js) },

//   MP_MODULE_ATTR_DELEGATION_ENTRY(&mp_module_js_attr),
// };
// STATIC MP_DEFINE_CONST_DICT(mp_module_js_globals,
// mp_module_js_globals_table);
