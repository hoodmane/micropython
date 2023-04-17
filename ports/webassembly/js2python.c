#define PY_SSIZE_T_CLEAN

#include "error_handling.h"
// #include "js2python.h"
#include "py/runtime.h"
#include "py/binary.h"
#include "py/mperrno.h"
#include "py/objint.h"
#include "py/gc.h"

#include <emscripten.h>

#include "jsmemops.h"


EMSCRIPTEN_KEEPALIVE mp_obj_t
_js2python_none()
{
  return mp_const_none;
}

EMSCRIPTEN_KEEPALIVE mp_obj_t
_js2python_true()
{
  return mp_const_true;
}

EMSCRIPTEN_KEEPALIVE mp_obj_t
_js2python_false()
{
  return mp_const_false;
}

EM_JS_REF(mp_obj_t, js2python_immutable, (JsRef id), {
  let value = Hiwire.get_value(id);
  let result = Module.js2python_convertImmutable(value, id);
  // clang-format off
  if (result !== undefined) {
    // clang-format on
    return result;
  }
  return 0;
});

EM_JS_REF(mp_obj_t, js2python_js, (JsRef id), {
  let value = Hiwire.get_value(id);
  let result = Module.js2python_convertImmutable(value, id);
  // clang-format off
  if (result !== undefined) {
    // clang-format on
    return result;
  }
  return _JsProxy_new(id);
})

mp_obj_t
js2python(JsRef id) {
  return js2python_js(id);
}

// clang-format on

#include "include_js_file.h"
#include "js2python.js"

int js2python_init(void) {
  return js2python_init_js();
}
