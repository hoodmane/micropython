#include "py/builtin.h"

#include "jsproxy.h"
#include "pyproxy.h"
#include "hiwire.h"

static mp_obj_t 
create_proxy(mp_obj_t self) {
  JsRef ref = pyproxy_new(self);
  mp_obj_t result = JsProxy_new(ref);
  hiwire_decref(ref);
  return result;
}
MP_DEFINE_CONST_FUN_OBJ_1(create_proxy_obj, create_proxy);

STATIC const mp_rom_map_elem_t mp_module_pyodide_core_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_pyodide_core) },
    { MP_ROM_QSTR(MP_QSTR_create_proxy), MP_ROM_PTR(&create_proxy_obj) }
};

STATIC MP_DEFINE_CONST_DICT(mp_module_pyodide_core_globals, mp_module_pyodide_core_globals_table);

const mp_obj_module_t mp_module_pyodide_core = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&mp_module_pyodide_core_globals,
};

MP_REGISTER_MODULE(MP_QSTR__pyodide_core, mp_module_pyodide_core);
