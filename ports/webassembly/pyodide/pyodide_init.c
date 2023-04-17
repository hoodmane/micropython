#include "py/runtime.h"
#include "py/builtin.h"

#include "emscripten.h"
#include "pyproxy.h"
#include "js2python.h"


EMSCRIPTEN_KEEPALIVE JsRef
pyimport(JsRef name) {
    nlr_buf_t nlr;
    if (nlr_push(&nlr) == 0) {
        mp_obj_t pyname = js2python(name);
        mp_obj_t result = mp_builtin___import__(1, &pyname);
        return pyproxy_new(result);
    } else {
        record_traceback(nlr.ret_val);
        return NULL;
    }
}


int hiwire_init(void);
int js2python_init(void);
int pyproxy_init(void);

void
pyodide_init(void) {
    hiwire_init();
    js2python_init();
    pyproxy_init();
    mp_init();
}

