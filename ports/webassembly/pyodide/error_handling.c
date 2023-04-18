#include <emscripten.h>

#include "py/objexcept.h"
#include "py/runtime.h"
#include "py/stream.h"

MP_DEFINE_EXCEPTION(JsException, Exception)

EMSCRIPTEN_KEEPALIVE void
raise_js_exception(char* type, char* msg, char* tb)
{
  mp_raise_msg_varg(&mp_type_JsException, MP_ERROR_TEXT("%s: %s"), type, msg);
}

EM_JS(void, set_exc, (char* tb, size_t tblen), {
  API.python_tb = (new TextDecoder()).decode(HEAP8.subarray(tb, tb + tblen));
});

EMSCRIPTEN_KEEPALIVE void
record_traceback(mp_obj_t exc)
{
  vstr_t vstr;
  mp_print_t print;
  vstr_init_print(&vstr, 8, &print);
  mp_obj_print_exception(&print, exc);
  set_exc(vstr.buf, vstr.len);
}
