#include "py/obj.h"

#include "hiwire.h"

mp_obj_t
JsProxy_new_with_this(JsRef ref, JsRef this);

mp_obj_t
JsProxy_new(JsRef ref);

bool
JsProxy_Check(mp_obj_t x);


JsRef
JsProxy_AsJs(mp_obj_t x);
