#include "hiwire.h"
#include "js2python.h"
#include "jsmemops.h"
// #include "jsproxy.h"
// #include "pyproxy.h"
#include "python2js.h"
#include <emscripten.h>
#include "pyproxy.h"

#include "py/objint.h"
#include "py/objstr.h"




static JsRef
_python2js_unicode(mp_obj_t x);

static inline JsRef
_python2js_immutable(mp_obj_t x);

///////////////////////////////////////////////////////////////////////////////
//
// Simple Converters
//
// These convert float, int, and unicode types. Used by python2js_immutable
// (which also handles bool and None).

static JsRef
_python2js_float(mp_obj_t x)
{
  mp_float_t val = mp_obj_float_get(x);
  return hiwire_double(val);
  // double x_double = PyFloat_AsDouble(x);
  // if (x_double == -1.0 && PyErr_Occurred()) {
  //   return NULL;
  // }
}

#include <stdio.h>

static JsRef
_python2js_long(mp_obj_t x)
{
  if (mp_obj_is_small_int(x)) {
    mp_int_t val = MP_OBJ_SMALL_INT_VALUE(x);
    return hiwire_int(val);
  }
  #define NUM_BYTES 20
  unsigned char data[NUM_BYTES] = {0};
  mp_obj_int_to_bytes_impl(x, false, NUM_BYTES, data);
  return hiwire_int_from_digits((unsigned int*)data, NUM_BYTES/4);
  #undef NUM_BYTES
}

static JsRef
_python2js_unicode(mp_obj_t x)
{
  GET_STR_DATA_LEN(x, str_data, str_len);
  return hiwire_string_utf8_len(str_data, str_len);
}

///////////////////////////////////////////////////////////////////////////////
//
// Container Converters
//
// These convert list, dict, and set types. We only convert objects that
// subclass list, dict, or set.
//
// One might consider trying to convert things that satisfy PyMapping_Check to
// maps and things that satisfy PySequence_Check to lists. However
// PyMapping_Check "returns 1 for Python classes with a __getitem__() method"
// and PySequence_Check returns 1 for classes with a __getitem__ method that
// don't subclass dict. For this reason, I think we should stick to subclasses.


/**
 * if x is NULL, fail
 * if x is Js_novalue, do nothing
 * in any other case, return x
 */
#define RETURN_IF_HAS_VALUE(x)                                                 \
  do {                                                                         \
    JsRef _fresh_result = x;                                                   \
    if (_fresh_result != Js_novalue) {                                         \
      return _fresh_result;                                                    \
    }                                                                          \
  } while (0)

/**
 * Convert x if x is an immutable python type for which there exists an
 * equivalent immutable JavaScript type. Otherwise return Js_novalue.
 *
 * Return type would be Option<JsRef>
 */
static inline JsRef
_python2js_immutable(mp_obj_t x)
{
  const mp_obj_type_t *type = mp_obj_get_type(x);
  if (x == mp_const_none) {
    return Js_undefined;
  } else if (x == mp_const_true) {
    return Js_true;
  } else if (x == mp_const_false) {
    return Js_false;
  } else if (type == &mp_type_int) {
    return _python2js_long(x);
  } else if (type == &mp_type_float) {
    return _python2js_float(x);
  } else if (type == &mp_type_str) {
    return _python2js_unicode(x);
  }
  return Js_novalue;
}

/**
 * If x is a wrapper around a JavaScript object, unwrap the JavaScript object
 * and return it. Otherwise, return Js_novalue.
 *
 * Return type would be Option<JsRef>
 */
static inline JsRef
_python2js_proxy(mp_obj_t x)
{
  // if (JsProxy_Check(x)) {
  //   return JsProxy_AsJs(x);
  // }
  return Js_novalue;
}

/**
 * Do a shallow conversion from python2js. Convert immutable types with
 * equivalent JavaScript immutable types, but all other types are proxied.
 *
 */
JsRef
python2js_inner(mp_obj_t x, JsRef proxies, bool track_proxies)
{
  RETURN_IF_HAS_VALUE(_python2js_immutable(x));
  RETURN_IF_HAS_VALUE(_python2js_proxy(x));
  if (track_proxies && proxies == NULL) {
    // PyErr_SetString(conversion_error, "No conversion known for x.");
    // FAIL();
  }
  JsRef proxy = pyproxy_new(x);
  // FAIL_IF_NULL(proxy);
  // if (track_proxies) {
  //   JsArray_Push_unchecked(proxies, proxy);
  // }
  return proxy;
// finally:
//   if (PyErr_Occurred()) {
//     if (!PyErr_ExceptionMatches(conversion_error)) {
//       _PyErr_FormatFromCause(conversion_error,
//                              "Conversion from python to javascript failed");
//     }
//   } else {
//     fail_test();
//     PyErr_SetString(internal_error, "Internal error occurred in python2js");
//   }
//   return NULL;
}

/**
 * Do a shallow conversion from python2js. Convert immutable types with
 * equivalent JavaScript immutable types.
 *
 * Other types are proxied and added to the list proxies (to allow easy memory
 * management later). If proxies is NULL, python2js will raise an error instead
 * of creating a proxy.
 */
JsRef
python2js_track_proxies(mp_obj_t x, JsRef proxies)
{
  return python2js_inner(x, proxies, true);
}

/**
 * Do a translation from Python to JavaScript. Convert immutable types with
 * equivalent JavaScript immutable types, but all other types are proxied.
 */
JsRef
python2js(mp_obj_t x)
{
  return python2js_inner(x, NULL, false);
}

