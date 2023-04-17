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


EM_JS(void, lib_init, (void), {
    FS.mkdir('/lib/');
    for (const dir of pylibManifest.dirs) {
        FS.mkdir('/lib/' + dir);
    }
    for (const [path, value] of pylibManifest.files) {
        FS.writeFile('/lib/' + path, value);
    }
})

int hiwire_init(void);
int js2python_init(void);
int pyproxy_init(void);

void
pyodide_init(void) {
    lib_init();
    hiwire_init();
    js2python_init();
    pyproxy_init();
    mp_init();
}

