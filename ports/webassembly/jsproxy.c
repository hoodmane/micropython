#include "jsproxy.h"

#include "py/obj.h"
#include "py/objstr.h"
#include "py/objmodule.h"
#include "py/runtime.h"

#include "hiwire.h"
#include "js2python.h"
#include "python2js.h"


#define MAYBE_RERAISE(nlr) do { \
    if(nlr.ret_val) { \
      nlr_raise(MP_OBJ_FROM_PTR(nlr.ret_val)); \
    } \
  } while(0) 

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
    mp_printf(print, "<JsProxy %d: %d>", (int)self->ref, pystr);
  } else {
    mp_printf(print, "%d", pystr);
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

EM_JS(void, console_log, (JsRef x), {
  console.log(Hiwire.get_value(x));
})

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
  if(nlr_push(&nlr) == 0) {
    proxies = JsArray_New();
    idargs = JsMethod_ConvertArgs(n_args, n_kw, args, proxies);
    idresult = hiwire_call_bound(self->ref, self->this, idargs);
    result = js2python(idresult);
    nlr_pop();
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

STATIC void JsProxy_attr(mp_obj_t self_in, qstr attr, mp_obj_t *dest) {
    JsProxy *self = MP_OBJ_TO_PTR(self_in);
    JsRef jsvalue = NULL;
    if (dest[0] == MP_OBJ_NULL) {
        // Load attribute.
        if (attr == MP_QSTR_new) {
            // Special case to handle construction of JS objects.
            return;
        }
        jsvalue = JsObject_GetString(self->ref, qstr_str(attr));
        dest[0] = js2python(jsvalue);
    } else if (dest[1] == MP_OBJ_NULL) {
        JsObject_DeleteString(self->ref, qstr_str(attr));
    } else {
        // Store attribute.
        jsvalue = js2python(dest[1]);
        JsObject_SetString(self->ref, qstr_str(attr), jsvalue);
        dest[0] = MP_OBJ_NULL;
    }
    hiwire_CLEAR(jsvalue);
}

// STATIC mp_obj_t JsMethod_construct(size_t n, const mp_obj_t *args, mp_map_t *kwargs) {
//     JsRef ref = JsProxy_get_ref(args[0]);
//     const char *arg1 = mp_obj_str_get_str(args[1]);
//     uint32_t out[3];
//     js_reflect_construct(arg0, arg1, out);
//     return convert_to_mp_obj_cside(out);
// }
// STATIC MP_DECLARE_CONST_FUN_OBJ_KW(jsobj_reflect_construct_obj, 1, jsobj_reflect_construct);


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
  attr, JsProxy_attr
  // iter, jsobj_getiter
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
  const mp_obj_type_t *type = mp_obj_get_type(x);

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
// STATIC MP_DEFINE_CONST_DICT(mp_module_js_globals, mp_module_js_globals_table);
