/*
 * sand_table.c
 *
 * $Author: why $
 * $Date: 2006-05-08 22:03:50 -0600 (Mon, 08 May 2006) $
 *
 * Copyright (C) 2006 why the lucky stiff
 */
#include <ruby.h>
#include <st.h>
#include <env.h>

extern st_table *rb_class_tbl;
/* extern st_table *rb_global_tbl; */
extern VALUE ruby_top_self;

typedef struct {
  st_table *tbl;
  st_table *globals;
  VALUE cObject;
  VALUE cModule;
  VALUE cClass;
  VALUE mKernel;
  VALUE oMain;
} sandkit;

static void
mark_sandbox(kit)
  sandkit *kit;
{
  rb_mark_tbl(kit->tbl);
  rb_mark_tbl(kit->globals);
  rb_gc_mark(kit->cObject);
  rb_gc_mark(kit->cModule);
  rb_gc_mark(kit->cClass);
  rb_gc_mark(kit->mKernel);
}

void
free_sandbox(kit)
  sandkit *kit;
{   
  st_free_table(kit->tbl);
  st_free_table(kit->globals);
  free(kit);
}
 
VALUE
sandbox_module_new(kit)
  sandkit *kit;
{
  NEWOBJ(mdl, struct RClass);
  OBJSETUP(mdl, kit->cModule, T_MODULE);
  
  mdl->super = 0;
  mdl->iv_tbl = 0;
  mdl->m_tbl = 0;
  mdl->m_tbl = st_init_numtable();
  
  return (VALUE)mdl;
}

VALUE
sandbox_define_module_id(kit, id)
  sandkit *kit;
  ID id;
{   
  VALUE mdl;

  mdl = sandbox_module_new(kit);
  rb_name_class(mdl, id);

  return mdl;
}

VALUE
sandbox_boot(kit, super)
  sandkit *kit;
  VALUE super;
{
  NEWOBJ(klass, struct RClass);
  OBJSETUP(klass, kit->cClass, T_CLASS);
      
  klass->super = super;
  klass->iv_tbl = 0;
  klass->m_tbl = 0;       /* safe GC */
  klass->m_tbl = st_init_numtable();
  
  OBJ_INFECT(klass, super);
  return (VALUE)klass;
}

VALUE
sandbox_metaclass(kit, obj, super)
  sandkit *kit;
  VALUE obj, super;
{
  VALUE klass = sandbox_boot(kit, super);
  FL_SET(klass, FL_SINGLETON);
  RBASIC(obj)->klass = klass;
  rb_singleton_class_attached(klass, obj);
  if (BUILTIN_TYPE(obj) == T_CLASS && FL_TEST(obj, FL_SINGLETON)) {
    RBASIC(klass)->klass = klass;
    RCLASS(klass)->super = RBASIC(rb_class_real(RCLASS(obj)->super))->klass;
  }
  else {
    VALUE metasuper = RBASIC(rb_class_real(super))->klass;

    /* metaclass of a superclass may be NULL at boot time */
    if (metasuper) {
        RBASIC(klass)->klass = metasuper;
    }
  }

  return klass;
}

static VALUE
sandbox_defclass(kit, name, super)
  sandkit *kit;
  char *name;
  VALUE super;
{   
  VALUE obj = sandbox_boot(kit, super);
  ID id = rb_intern(name);

  rb_name_class(obj, id);
  st_add_direct(kit->tbl, id, obj);
  rb_const_set((kit->cObject ? kit->cObject : obj), id, obj);
  return obj;
}


VALUE
sandbox_defmodule(kit, name)
  sandkit *kit;
  const char *name;
{
  VALUE module;
  ID id;

  id = rb_intern(name);
  if (rb_const_defined(kit->cObject, id)) {
    module = rb_const_get(kit->cObject, id);
    if (TYPE(module) == T_MODULE)
        return module;
    rb_raise(rb_eTypeError, "%s is not a module", rb_obj_classname(module));
  }
  module = sandbox_define_module_id(kit, id);
  st_add_direct(kit->tbl, id, module);
  rb_const_set(kit->cObject, id, module);

  return module;
}

static VALUE sandbox_alloc_obj _((VALUE));
static VALUE
sandbox_alloc_obj(klass)
  VALUE klass; 
{   
  NEWOBJ(obj, struct RObject);
  OBJSETUP(obj, klass, T_OBJECT);
  return (VALUE)obj;
}

VALUE sandbox_alloc _((VALUE));
VALUE 
sandbox_alloc(class)
    VALUE class;
{
  VALUE metaclass;
  sandkit *kit = ALLOC(sandkit);

  kit->tbl = st_init_numtable();
  kit->globals = st_init_numtable();
  kit->cObject = 0;

  kit->cObject = sandbox_defclass(kit, "Object", 0);
  kit->cModule = sandbox_defclass(kit, "Module", kit->cObject);
  kit->cClass =  sandbox_defclass(kit, "Class",  kit->cModule);

  metaclass = sandbox_metaclass(kit, kit->cObject, kit->cClass);
  metaclass = sandbox_metaclass(kit, kit->cModule, metaclass);
  metaclass = sandbox_metaclass(kit, kit->cClass, metaclass);

  kit->mKernel = sandbox_defmodule(kit, "Kernel");
  rb_include_module(kit->cObject, kit->mKernel);
  rb_define_alloc_func(kit->cObject, sandbox_alloc_obj);
  kit->oMain = rb_obj_alloc(kit->cObject);

  return Data_Wrap_Struct( class, mark_sandbox, free_sandbox, kit );
}

typedef struct {
  VALUE *argv;
  sandkit *kit;
  sandkit *norm;
} go_cart;

VALUE
sandbox_go_go_go(go)
  go_cart *go;
{
  return rb_obj_instance_eval(1, go->argv, go->kit->oMain);
}

VALUE
sandbox_whoa_whoa_whoa(go)
  go_cart *go;
{
  /* okay, move it all back */
  sandkit *norm = go->norm;
  rb_class_tbl = norm->tbl;
  /* rb_global_tbl = norm->globals; */
  rb_cObject = norm->cObject;
  rb_cModule = norm->cModule;
  rb_cClass = norm->cClass;
  rb_mKernel = norm->mKernel;
  ruby_top_self = norm->oMain;
  free(go->norm);
  free(go);
}

VALUE
sandbox_eval( self, str )
  VALUE self, str;
{
  sandkit *kit, *norm;
  go_cart *go;
  VALUE val;
  Data_Get_Struct( self, sandkit, kit );

  /* save everything */
  norm = ALLOC(sandkit);
  norm->tbl = rb_class_tbl;
  /* norm->globals = rb_global_tbl; */
  norm->cObject = rb_cObject;
  norm->cModule = rb_cModule;
  norm->cClass = rb_cClass;
  norm->mKernel = rb_mKernel;
  norm->oMain = ruby_top_self;

  /* replace everything */
  rb_class_tbl = kit->tbl;
  /* rb_global_tbl = kit->globals; */
  rb_cObject = kit->cObject;
  rb_cModule = kit->cModule;
  rb_cClass = kit->cClass;
  rb_mKernel = kit->mKernel;
  ruby_top_self = kit->oMain;

  go = ALLOC(go_cart);
  go->argv = ALLOC(VALUE);
  go->argv[0] = str;
  go->norm = norm;
  go->kit = kit;

  rb_ensure(sandbox_go_go_go, (VALUE)go, sandbox_whoa_whoa_whoa, (VALUE)go);

  return val;
}

void Init_sand_table()
{
  VALUE cSandbox = rb_define_class("Sandbox", rb_cObject);
  rb_define_alloc_func( cSandbox, sandbox_alloc );
  rb_define_method( cSandbox, "eval", sandbox_eval, 1 );
}