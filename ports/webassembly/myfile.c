#include <stdio.h>
#include <string.h>

#include "py/runtime.h"
#include "py/binary.h"
#include "py/mperrno.h"
#include "py/objint.h"
#include "py/gc.h"

#include "hiwire.h"
#include "python2js.h"
#include "pyproxy.h"


// STATIC const mp_obj_type_t my_type;

typedef struct _mp_obj_jsproxy_t {
    mp_obj_base_t base;
    JsRef js;
} mp_obj_jsproxy_t;


STATIC void jsproxy_print(const mp_print_t *print, mp_obj_t self_in, mp_print_kind_t kind) {
    mp_obj_jsproxy_t* self = MP_OBJ_TO_PTR(self_in);
    (void)kind;
    mp_printf(print, "<JsProxy %d>", self->js);
}


STATIC mp_obj_t jsproxy_call(mp_obj_t self_in, size_t n_args, size_t n_kw, const mp_obj_t *args) {
    mp_obj_jsproxy_t* self = MP_OBJ_TO_PTR(self_in);
    return mp_obj_new_int((int)(self->js));

// error:
    mp_raise_TypeError(MP_ERROR_TEXT("don't know how to pass object to native function"));
}

STATIC MP_DEFINE_CONST_OBJ_TYPE(
    jsproxy_type,
    MP_QSTR_JsProxy,
    MP_TYPE_FLAG_NONE,
    print, jsproxy_print,
    call, jsproxy_call
);

EM_JS(JsRef, get_globals, (), {
    return Hiwire.new_value(globalThis);
})

STATIC mp_obj_t jsproxy_create_global(void) {
    mp_obj_jsproxy_t *o = mp_obj_malloc(mp_obj_jsproxy_t, &jsproxy_type);
    o->js = get_globals();
    return MP_OBJ_FROM_PTR(o);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_0(jsproxy_create_global_obj, jsproxy_create_global);

STATIC mp_obj_t jsproxy_create(mp_obj_t obj) {
    mp_obj_jsproxy_t *o = mp_obj_malloc(mp_obj_jsproxy_t, &jsproxy_type);
    o->js = (JsRef)3;
    return MP_OBJ_FROM_PTR(o);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(jsproxy_create_obj, jsproxy_create);

STATIC mp_obj_t mod_test_print(mp_obj_t obj) {
    mp_obj_print((mp_obj_t)obj, PRINT_STR);
    return mp_obj_new_int(7);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(mod_test_print_obj, mod_test_print);

STATIC mp_obj_t to_js(mp_obj_t obj) {
    JsRef x = python2js(obj);
    return mp_obj_new_int((int)x);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(to_js_obj, to_js);


STATIC mp_obj_t pyproxy(mp_obj_t obj) {
    JsRef x = pyproxy_new(obj);
    return mp_obj_new_int((int)x);
}
STATIC MP_DEFINE_CONST_FUN_OBJ_1(pyproxy_obj, pyproxy);


STATIC const mp_rom_map_elem_t mp_module_mytestmodule_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_mytestmodule) },
    { MP_ROM_QSTR(MP_QSTR_test_print), MP_ROM_PTR(&mod_test_print_obj) },
    { MP_ROM_QSTR(MP_QSTR_jsproxy_create), MP_ROM_PTR(&jsproxy_create_obj) },
    { MP_ROM_QSTR(MP_QSTR_global), MP_ROM_PTR(&jsproxy_create_global_obj) },
    { MP_ROM_QSTR(MP_QSTR_to_js), MP_ROM_PTR(&to_js_obj) },
    { MP_ROM_QSTR(MP_QSTR_pyproxy), MP_ROM_PTR(&pyproxy_obj) },
};

STATIC MP_DEFINE_CONST_DICT(mp_module_mytestmodule_globals, mp_module_mytestmodule_globals_table);

const mp_obj_module_t mp_module_mytestmodule = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_mytestmodule_globals,
};

MP_REGISTER_MODULE(MP_QSTR_mytestmodule, mp_module_mytestmodule);

